// WfsRenderer.cpp — ver header para la descripción del modelo físico y la
// arquitectura glitch-free (eliminación de micro-cortes).
#include "WfsRenderer.hpp"

#if defined(__x86_64__) || defined(__i386__)
  #include <immintrin.h>
#endif

namespace {
// Guarda anti-denormales FTZ/DAZ (mismo patron que hrtf_convolver.cpp):
// las lineas de delay y los one-poles de suavizado arrastran residuos
// subnormales (~1e-38) donde la CPU degrada 10-100x por microcode assist
// -> picos de carga en el hilo de audio -> underruns = micro-cortes.
inline void enableWfsDenormalGuard() noexcept {
#if defined(__x86_64__) || defined(__i386__)
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#elif defined(__aarch64__)
    uint64_t fpcr; __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (1ULL << 24);
    __asm__ volatile("msr fpcr, %0" :: "r"(fpcr));
#endif
}
} // namespace

namespace ivanna::spatial {

bool WfsRenderer::init(float sampleRate, int blockSize, int numSpeakers) noexcept {
    if (sampleRate <= 0.f || blockSize <= 0) return false;
    if (numSpeakers < 4) numSpeakers = 4;
    if (numSpeakers > kMaxSpeakers) numSpeakers = kMaxSpeakers;
    sampleRate_  = sampleRate;
    blockSize_   = blockSize;
    // Fase 2 (geometría 3D real): si ya se cargó un layout explícito
    // (setSpeakerLayout3D llamado antes de esta init(), p.ej. desde
    // omega_apply_snapshot antes de la primera activación perezosa de
    // WFS) se respeta su numSpeakers_ (7) — init() nunca pisa una
    // geometría real ya cargada con el parámetro genérico.
    if (!explicitLayout_) {
        numSpeakers_ = numSpeakers;
    }

    // Delay máximo necesario: fuente más lejana permitida + diámetro del
    // array (peor caso geométrico) + margen de un bloque.
    const float maxDistS = (kMaxObjectDist + 2.f * kArrayRadiusM) / kSpeedOfSound;
    maxDelayTap_ = static_cast<int>(maxDistS * sampleRate_) + blockSize_ + 4;

    if (!explicitLayout_) {
        rebuildGeometry();
    }

    // FIX (guarda de anti-aliasing espacial, antes inalcanzable): separación
    // real entre altavoces adyacentes = circunferencia / N. Con los valores
    // por defecto (16 altavoces, radio 1.5 m) da f_alias ≈ 291 Hz.
    constexpr float kPi = 3.14159265358979323846f;
    const float speakerSpacingM = (2.f * kPi * kArrayRadiusM) / static_cast<float>(numSpeakers_);
    aliasGuard_.init(speakerSpacingM, sampleRate_);

    // Preasignación TOTAL: kMaxObjects líneas de fuente + kMaxObjects ×
    // numSpeakers taps. process() jamás asigna memoria.
    for (auto& slot : slots_) {
        slot = SourceSlot{};
        slot.line.assign(static_cast<size_t>(maxDelayTap_), 0.f);
    }
    taps_.assign(static_cast<size_t>(kMaxObjects) * static_cast<size_t>(numSpeakers_),
                 DelayTap{});
    numActiveObjects_ = 0;
    return true;
}

void WfsRenderer::setSpeakerLayout3D(const float* dx, const float* dyFwd, const float* dz,
                                      int count) noexcept {
    if (dx == nullptr || dyFwd == nullptr || dz == nullptr || count <= 0) return;
    if (count > kMaxSpeakers) count = kMaxSpeakers;
    numSpeakers_ = count;
    explicitLayout_ = true;
    for (int s = 0; s < count; ++s) {
        speakerRelX_[static_cast<size_t>(s)] = dx[s];
        speakerRelY_[static_cast<size_t>(s)] = dyFwd[s];
        speakerRelZ_[static_cast<size_t>(s)] = dz[s];
        // Azimut/ILD/ITD desde el plano horizontal (mismo modelo que
        // rebuildGeometry: pan = -sin(az) con az medido desde atrás). La
        // altura no participa en ITD/ILD (simplificación estándar: los
        // cues interaurales por defecto no codifican elevación).
        const float az = std::atan2(-dx[s], -dyFwd[s]); // atrás=+π, coherente con rebuildGeometry
        speakerAzimuth_[static_cast<size_t>(s)] = az;
        const float pan = -std::sin(az);
        const float theta = (pan + 1.f) * 0.25f * 3.14159265358979323846f;
        speakerGainL_[static_cast<size_t>(s)] = std::cos(theta) * 1.41421356f;
        speakerGainR_[static_cast<size_t>(s)] = std::sin(theta) * 1.41421356f;
        const int itd = static_cast<int>(kMaxItdS * sampleRate_ * std::fabs(pan) + 0.5f);
        if (pan >= 0.f) { speakerItdL_[static_cast<size_t>(s)] = itd; speakerItdR_[static_cast<size_t>(s)] = 0; }
        else            { speakerItdL_[static_cast<size_t>(s)] = 0;   speakerItdR_[static_cast<size_t>(s)] = itd; }
    }
}

void WfsRenderer::reset() noexcept {
    // Reset explícito (cambio de sesión/ruta): limpia historiales. No se usa
    // en movimiento de fuentes — solo aquí está permitido borrar historia.
    for (auto& slot : slots_) {
        for (auto& v : slot.line) v = 0.f;
        slot.writePos = 0;
        slot.actEnv = slot.actTarget = slot.active ? 1.f : 0.f;
    }
    for (auto& h : itdHist_)
        for (auto& v : h) v = 0.f;
    for (auto& p : itdPos_) p = 0;
    aliasGuard_.reset();
}

void WfsRenderer::rebuildGeometry() noexcept {
    constexpr float kPi = 3.14159265358979323846f;
    for (int s = 0; s < numSpeakers_; ++s) {
        // Array circular completo, AZIMUT MEDIDO DESDE ATRÁS (-y), sentido
        // horario: el frente (+y) queda exactamente en az=±π donde pan=0 →
        // ganancias L/R iguales (simetría frontal exacta con N par).
        const float az = 2.f * kPi * static_cast<float>(s) / static_cast<float>(numSpeakers_) - kPi;
        speakerAzimuth_[s] = az;
        // ILD senoidal clásico (tangente del azimut → ganancias constant-power).
        // Con az medido desde atrás, el altavoz más próximo a +x tiene
        // az=-π/2; con pan=-sin(az) ese altavoz da pan=+1 → ILD a derecha.
        const float pan = -std::sin(az);                   // -1 (izq) … +1 (der)
        const float theta = (pan + 1.f) * 0.25f * kPi;     // 0…π/2
        speakerGainL_[s] = std::cos(theta) * 1.41421356f;  // normalizado a ±3 dB máx
        speakerGainR_[s] = std::sin(theta) * 1.41421356f;
        // ITD: retardo interaural máximo cuando el altavoz está lateral.
        const int itd = static_cast<int>(kMaxItdS * sampleRate_ * std::fabs(pan) + 0.5f);
        if (pan >= 0.f) { speakerItdL_[s] = itd; speakerItdR_[s] = 0; }
        else            { speakerItdL_[s] = 0;   speakerItdR_[s] = itd; }
    }
}

int WfsRenderer::findSlot(int id) const noexcept {
    for (int i = 0; i < kMaxObjects; ++i)
        if (slots_[static_cast<size_t>(i)].active && slots_[static_cast<size_t>(i)].id == id)
            return i;
    return -1;
}

int WfsRenderer::allocSlot() noexcept {
    for (int i = 0; i < kMaxObjects; ++i)
        if (!slots_[static_cast<size_t>(i)].active) return i;
    return -1;
}

void WfsRenderer::setObject(int id, float x, float y, float gain) noexcept {
    if (id < 0) return;
    // Clamp de distancia física (una fuente a 1 km rompería la aproximación).
    const float d = std::sqrt(x * x + y * y);
    if (d > kMaxObjectDist) {
        const float sc = kMaxObjectDist / d;
        x *= sc; y *= sc;
    }

    int si = findSlot(id);
    if (si >= 0) {
        // ── Movimiento de fuente existente: SOLO se recalculan los
        // objetivos de delay/ganancia. El historial de audio NO se toca;
        // los valores efectivos persiguen al objetivo con un one-pole por
        // muestra en process() → glide sin click (antes: rebuildDelays()
        // borraba todas las líneas = micro-corte garantizado).
        SourceSlot& slot = slots_[static_cast<size_t>(si)];
        slot.x = x; slot.y = y; slot.gain = gain;
        slot.actTarget = 1.f;   // revive si estaba en fade-out
        computeTapTargets(si);
        return;
    }

    // ── Nacimiento de fuente nueva: línea fresca (ya está a ceros desde
    // init() o desde su último retiro completo), smooth snappeado al
    // objetivo (no hay audio previo que hacer glisar — el nacimiento es
    // naturalmente limpio porque la línea empieza en silencio físico).
    si = allocSlot();
    if (si < 0) return;         // array de fuentes lleno — degradar en silencio
    SourceSlot& slot = slots_[static_cast<size_t>(si)];
    slot.id = id; slot.active = true;
    slot.x = x; slot.y = y; slot.gain = gain;
    slot.actEnv = 1.f; slot.actTarget = 1.f; slot.envFrom = 1.f; slot.envDelta = 0.f;
    slot.writePos = 0;
    ++numActiveObjects_;
    computeTapTargets(si);
    // Snap inicial: sin historial previo no hay nada que suavizar.
    for (int s = 0; s < numSpeakers_; ++s) {
        DelayTap& tap = taps_[static_cast<size_t>(si) * static_cast<size_t>(numSpeakers_)
                              + static_cast<size_t>(s)];
        tap.delaySmooth = tap.delayTarget;
        tap.gainSmooth  = tap.gainTarget;
    }
}

void WfsRenderer::removeObject(int id) noexcept {
    const int si = findSlot(id);
    if (si < 0) return;
    // NO se borra la línea ni el estado: se marca fade-out. process() baja
    // actEnv a 0 a lo largo de un bloque y libera la ranura al final —
    // cortar una fuente sonando en seco era el otro click clásico.
    slots_[static_cast<size_t>(si)].actTarget = 0.f;
}

void WfsRenderer::clearObjects() noexcept {
    for (auto& slot : slots_)
        if (slot.active) slot.actTarget = 0.f;
}

void WfsRenderer::computeTapTargets(int si) noexcept {
    const SourceSlot& o = slots_[static_cast<size_t>(si)];
    const float od = std::sqrt(o.x * o.x + o.y * o.y);
    for (int s = 0; s < numSpeakers_; ++s) {
        DelayTap& tap = taps_[static_cast<size_t>(si) * static_cast<size_t>(numSpeakers_)
                              + static_cast<size_t>(s)];
        // Posición del altavoz: geometría 3D real explícita (Fase 2) si se
        // configuró vía setSpeakerLayout3D(), si no el array circular
        // generado por rebuildGeometry() (comportamiento original).
        float sx, sy, sz;
        if (explicitLayout_) {
            sx = speakerRelX_[static_cast<size_t>(s)];
            sy = speakerRelY_[static_cast<size_t>(s)];
            sz = speakerRelZ_[static_cast<size_t>(s)];
        } else {
            sx = -kArrayRadiusM * std::sin(speakerAzimuth_[s]);
            sy = -kArrayRadiusM * std::cos(speakerAzimuth_[s]);
            sz = 0.f; // fuentes y array circular viven a la altura del oído
        }
        const float dx = o.x - sx, dy = o.y - sy, dzh = 0.f - sz; // objeto a altura del oído
        const float d = std::sqrt(dx * dx + dy * dy + dzh * dzh); // distancia 3D real (Fase 2)
        // Delay fraccionario objetivo (interpolación lineal en la lectura).
        const float delaySampF = (d / kSpeedOfSound) * sampleRate_;
        const float maxTap = static_cast<float>(maxDelayTap_ - blockSize_ - 4);
        tap.delayTarget = delaySampF < 0.f ? 0.f : (delaySampF > maxTap ? maxTap : delaySampF);
        // Driving function WFS 2.5D (aprox.): atenuación de onda cilíndrica
        // 1/sqrt(d) referida a kRefDistanceM, por peso de focalización coseno.
        // FIX (geometría 3D real, 2026-09-19): cosA dividía por kArrayRadiusM
        // (constante del array circular, exacta ahí por construcción) incluso
        // con layout explícito, donde la distancia horizontal real de cada
        // altavoz varía mucho (subwoofer ~0.5 m, satélites elevados ~4 m) —
        // desbalanceaba el peso de foco entre altavoces. Se usa la distancia
        // horizontal REAL de cada altavoz (spkDist), idéntica a kArrayRadiusM
        // en el caso circular (sin cambio ahí) y correcta en el explícito.
        float focus = 1.f;
        const float spkDist = std::sqrt(sx * sx + sy * sy);
        if (od > 1e-6f && spkDist > 1e-6f) {
            const float cosA = (o.x * sx + o.y * sy) / (od * spkDist);
            focus = cosA > 0.f ? cosA : 0.f;
        }
        const float att = std::sqrt(kRefDistanceM / (d > kRefDistanceM ? d : kRefDistanceM));
        tap.gainTarget = o.gain * att * focus;
    }
}

void WfsRenderer::process(const float* const* objectInputs, int numObjects,
                          float* outL, float* outR, int frames) noexcept {
    if (frames <= 0 || outL == nullptr || outR == nullptr) return;
    if (numActiveObjects_ == 0) return;
    if (enabledTarget_ <= 0.0f && enabledMix_ <= 0.0f) return;
    if (objectInputs == nullptr || numObjects <= 0) return;
    enableWfsDenormalGuard();   // FTZ/DAZ: sin microcode assists en el hilo RT

    const int M = maxDelayTap_;
    const float invFrames = 1.0f / static_cast<float>(frames);

    // ── 0) Envolventes de activación: rampa lineal por bloque hacia el
    //       objetivo (fade-out glitch-free en removeObject; las fuentes
    //       vivas permanecen en 1). Se calcula el paso ANTES del bloque.
    int inputIdx = 0;
    for (int si = 0; si < kMaxObjects; ++si) {
        SourceSlot& slot = slots_[static_cast<size_t>(si)];
        if (!slot.active) continue;
        slot.envFrom  = slot.actEnv;
        slot.envDelta = slot.actTarget - slot.actEnv;   // tramo del bloque
        // Mapeo input i → i-ésima fuente viva en orden de ranura: idéntico
        // al orden de registro de la versión anterior. Las fuentes en
        // fade-out NO consumen entrada (su señal ya no llega del caller).
        if (slot.actTarget > 0.f) {
            const float* in = (inputIdx < numObjects) ? objectInputs[inputIdx] : nullptr;
            ++inputIdx;
            // ── 1) Driving: escribir la fuente en SU línea (una por fuente).
            //       Si el caller no pasó entrada para una fuente viva, se
            //       escribe silencio — físicamente la fuente calló; mantener
            //       historia consistente (leer historia vieja sería un loop
            //       del pasado, audible como estutter).
            int wp = slot.writePos;
            if (in != nullptr) {
                for (int n = 0; n < frames; ++n) {
                    // GUARDA NaN/Inf (Fase 7: recuperación tras fallo): un
                    // NaN entrando a la línea circular la envenena PARA
                    // SIEMPRE (NaN*0=NaN — el historial nunca se limpia solo)
                    // y softLimit() no filtra NaN (todas sus ramas lo
                    // propagan). Se sanea en la ÚNICA puerta de entrada:
                    // muestra no finita -> silencio físico, el campo se
                    // recupera solo en el siguiente bloque.
                    const float v = in[n];
                    slot.line[static_cast<size_t>(wp)] = std::isfinite(v) ? v : 0.f;
                    wp = (wp + 1) % M;
                }
            } else {
                for (int n = 0; n < frames; ++n) {
                    slot.line[static_cast<size_t>(wp)] = 0.f;
                    wp = (wp + 1) % M;
                }
            }
            slot.writePos = wp;
        }
    }

    // ── 2+3) Síntesis del campo y mezcla binaural en UN solo pase por
    //        muestra. Los taps leen la línea de SU fuente con su propio
    //        delay fraccionario suavizado y su ganancia suavizada; el
    //        one-pole por muestra (kSmoothCoeff) hace que cualquier
    //        movimiento de la fuente sea un glide continuo — no hay salto
    //        de fase ni de amplitud, por tanto NO hay micro-corte.
    const float norm = 1.0f / std::sqrt(static_cast<float>(numSpeakers_));
    for (int n = 0; n < frames; ++n) {
        // Avance de envolventes por muestra (rampa lineal exacta en 1 bloque).
        // Envolvente con forma smoothstep t^2*(3-2t): derivada CERO al
        // inicio y al fin del fade -> el retiro/nacimiento de una fuente no
        // produce el click de esquina de la rampa lineal. f(1)=1 exacto.
        const float tt = static_cast<float>(n + 1) * invFrames;
        const float fshape = tt * tt * (3.0f - 2.0f * tt);
        for (int si = 0; si < kMaxObjects; ++si) {
            SourceSlot& slot = slots_[static_cast<size_t>(si)];
            if (!slot.active || slot.envDelta == 0.f) continue;
            slot.actEnv = slot.envFrom + slot.envDelta * fshape;
            if (slot.actEnv < 0.f) slot.actEnv = 0.f;
            if (slot.actEnv > 1.f) slot.actEnv = 1.f;
        }

        float accL = 0.f, accR = 0.f;
        for (int s = 0; s < numSpeakers_; ++s) {
            float spk = 0.f;
            for (int si = 0; si < kMaxObjects; ++si) {
                const SourceSlot& slot = slots_[static_cast<size_t>(si)];
                if (!slot.active) continue;
                DelayTap& tap = taps_[static_cast<size_t>(si) * static_cast<size_t>(numSpeakers_)
                                      + static_cast<size_t>(s)];
                // writePos apunta una posición DESPUÉS de la última muestra
                // escrita; la muestra n del bloque vive (frames-1-n) atrás,
                // más el delay físico suavizado del par (fuente→altavoz).
                const float ds = tap.delaySmooth;
                const int di = static_cast<int>(ds);
                const float frac = ds - static_cast<float>(di);
                const int back = frames - 1 - n + di;
                int rp  = (slot.writePos - back) % M;
                if (rp < 0) rp += M;
                int rp2 = rp - 1; if (rp2 < 0) rp2 += M;
                const float a = slot.line[static_cast<size_t>(rp)];
                const float b = slot.line[static_cast<size_t>(rp2)];
                spk += (a + (b - a) * frac) * tap.gainSmooth * slot.actEnv;
                // Suavizado por muestra: persecución glitch-free del objetivo.
                tap.delaySmooth += (tap.delayTarget - tap.delaySmooth) * kSmoothCoeff;
                tap.gainSmooth  += (tap.gainTarget  - tap.gainSmooth)  * kSmoothCoeff;
                // Snap: error sub-audible -> clavar al objetivo. Elimina la
                // persecucion asintotica en regimen denormal y la deriva.
                if (std::fabs(tap.delayTarget - tap.delaySmooth) < 5e-3f)
                    tap.delaySmooth = tap.delayTarget;
                if (std::fabs(tap.gainTarget - tap.gainSmooth) < 1e-4f)
                    tap.gainSmooth = tap.gainTarget;
            }
            // ITD entero por altavoz + ILD constant-power.
            auto& hist = itdHist_[static_cast<size_t>(s)];
            int& hp = itdPos_[static_cast<size_t>(s)];
            hist[static_cast<size_t>(hp)] = spk;
            const float vL = hist[static_cast<size_t>((hp - speakerItdL_[s]) & 63)];
            const float vR = hist[static_cast<size_t>((hp - speakerItdR_[s]) & 63)];
            accL += vL * speakerGainL_[s];
            accR += vR * speakerGainR_[s];
            hp = (hp + 1) & 63;
        }
        // FIX (guarda de anti-aliasing espacial, antes definida pero jamás
        // llamada desde ningún lado del árbol): se aplica al campo YA
        // reconstruido (acumulado sobre los altavoces virtuales), antes del
        // limitador — es la reconstrucción de onda la que necesita
        // suavizarse por encima de f_alias, no la señal ya comprimida.
        aliasGuard_.process(accL, accR);
        // Anti-clip (=> anti-tronidos): lineal bajo |1.0|, compresion suave arriba.
        // GUARDA NaN/Inf de salida: si pese a todo el campo acumulado no es
        // finito (estado corrupto heredado de antes del saneo de entrada),
        // se sustituye por silencio en ESTE bloque — la cadena downstream
        // (mixer/effect/salida hardware) jamás recibe un NaN desde WFS.
        const float outLRaw = accL * norm, outRRaw = accR * norm;
        outL[n] += std::isfinite(outLRaw) ? softLimit(outLRaw) : 0.f;
        outR[n] += std::isfinite(outRRaw) ? softLimit(outRRaw) : 0.f;
    }

    // ── 4) Retiro diferido: las fuentes cuyo fade-out terminó se liberan
    //       DESPUÉS del bloque (su línea queda a ceros de forma natural:
    //       durante el fade escribimos... su señal real; al liberar, la
    //       línea se limpia para su próximo nacimiento — hilo de control,
    //       fuera del pase caliente).
    for (int si = 0; si < kMaxObjects; ++si) {
        SourceSlot& slot = slots_[static_cast<size_t>(si)];
        if (slot.active && slot.actTarget == 0.f && slot.actEnv <= 0.f) {
            slot.active = false;
            slot.id = -1;
            for (auto& v : slot.line) v = 0.f;
            slot.writePos = 0;
            --numActiveObjects_;
        }
    }
}

void WfsRenderer::blendWithBypass(const float* dryL, const float* dryR,
                                   const float* wetL, const float* wetR,
                                   float* outL, float* outR, int frames) noexcept {
    if (frames <= 0 || dryL == nullptr || dryR == nullptr ||
        wetL == nullptr || wetR == nullptr || outL == nullptr || outR == nullptr) return;
    const float step = 1.0f / (sampleRate_ * 0.015f);
    for (int n = 0; n < frames; ++n) {
        if (enabledMix_ < enabledTarget_)      enabledMix_ = std::min(enabledMix_ + step, enabledTarget_);
        else if (enabledMix_ > enabledTarget_) enabledMix_ = std::max(enabledMix_ - step, enabledTarget_);
        const float t = enabledMix_;
        const float shaped = t * t * (3.0f - 2.0f * t);
        outL[n] = dryL[n] * (1.0f - shaped) + wetL[n] * shaped;
        outR[n] = dryR[n] * (1.0f - shaped) + wetR[n] * shaped;
    }
}

} // namespace ivanna::spatial
