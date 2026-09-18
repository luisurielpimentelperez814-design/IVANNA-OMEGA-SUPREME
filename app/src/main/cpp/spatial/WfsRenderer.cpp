// WfsRenderer.cpp — ver header para la descripción del modelo físico y la
// arquitectura glitch-free (eliminación de micro-cortes).
#include "WfsRenderer.hpp"

namespace ivanna::spatial {

bool WfsRenderer::init(float sampleRate, int blockSize, int numSpeakers) noexcept {
    if (sampleRate <= 0.f || blockSize <= 0) return false;
    if (numSpeakers < 4) numSpeakers = 4;
    if (numSpeakers > kMaxSpeakers) numSpeakers = kMaxSpeakers;
    sampleRate_  = sampleRate;
    blockSize_   = blockSize;
    numSpeakers_ = numSpeakers;

    // Delay máximo necesario: fuente más lejana permitida + diámetro del
    // array (peor caso geométrico) + margen de un bloque.
    const float maxDistS = (kMaxObjectDist + 2.f * kArrayRadiusM) / kSpeedOfSound;
    maxDelayTap_ = static_cast<int>(maxDistS * sampleRate_) + blockSize_ + 4;

    rebuildGeometry();

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
    slot.actEnv = 1.f; slot.actTarget = 1.f; slot.envStep = 0.f;
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
        // Posición del altavoz (az medido desde atrás → frente = ±π).
        const float sx = -kArrayRadiusM * std::sin(speakerAzimuth_[s]);
        const float sy = -kArrayRadiusM * std::cos(speakerAzimuth_[s]);
        const float dx = o.x - sx, dy = o.y - sy;
        const float d = std::sqrt(dx * dx + dy * dy);
        // Delay fraccionario objetivo (interpolación lineal en la lectura).
        const float delaySampF = (d / kSpeedOfSound) * sampleRate_;
        const float maxTap = static_cast<float>(maxDelayTap_ - blockSize_ - 4);
        tap.delayTarget = delaySampF < 0.f ? 0.f : (delaySampF > maxTap ? maxTap : delaySampF);
        // Driving function WFS 2.5D (aprox.): atenuación de onda cilíndrica
        // 1/sqrt(d) referida a kRefDistanceM, por peso de focalización coseno.
        float focus = 1.f;
        if (od > 1e-6f) {
            const float cosA = (o.x * sx + o.y * sy) / (od * kArrayRadiusM);
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
    if (objectInputs == nullptr || numObjects <= 0) return;

    const int M = maxDelayTap_;
    const float invFrames = 1.0f / static_cast<float>(frames);

    // ── 0) Envolventes de activación: rampa lineal por bloque hacia el
    //       objetivo (fade-out glitch-free en removeObject; las fuentes
    //       vivas permanecen en 1). Se calcula el paso ANTES del bloque.
    int inputIdx = 0;
    for (int si = 0; si < kMaxObjects; ++si) {
        SourceSlot& slot = slots_[static_cast<size_t>(si)];
        if (!slot.active) continue;
        slot.envStep = (slot.actTarget - slot.actEnv) * invFrames;
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
                    slot.line[static_cast<size_t>(wp)] = in[n];
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
        for (int si = 0; si < kMaxObjects; ++si) {
            SourceSlot& slot = slots_[static_cast<size_t>(si)];
            if (!slot.active || slot.envStep == 0.f) continue;
            slot.actEnv += slot.envStep;
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
        outL[n] += accL * norm;
        outR[n] += accR * norm;
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

} // namespace ivanna::spatial


// ═══ Mejora magistral 2026-09-18: guarda de anti-aliasing espacial ═══════════
// WFS con array discreto de N altavoces sufre aliasing espacial por encima de
// f_alias = c / (2 * dx), dx = separación entre altavoces. Por encima, el frente
// de onda se reconstruye con artefactos (fantasmas). Solución de referencia
// (Spors & Ahrens): atenuación suave por encima de f_alias con un one-pole
// por canal, calculada desde la geometría real del array. No degrada el audio
// por debajo de f_alias — solo evita que la reconstrucción mienta arriba.
namespace ivanna { namespace wfs {
    inline float spatialAliasCutoffHz(float speakerSpacingM, float c = 343.0f) noexcept {
        return (speakerSpacingM > 0.f) ? c / (2.f * speakerSpacingM) : 8000.f;
    }
    // One-pole lowpass por canal: y += a(x - y), a = 1 - exp(-2π f_c / sr)
    struct SpatialAliasGuard {
        float a = 1.f, yL = 0.f, yR = 0.f;
        void init(float spacingM, float sampleRate) noexcept {
            const float fc = spatialAliasCutoffHz(spacingM);
            a = 1.f - std::exp(-2.f * 3.14159265f * fc / sampleRate);
        }
        inline void process(float& L, float& R) noexcept {
            yL += a * (L - yL); yR += a * (R - yR); L = yL; R = yR;
        }
        void reset() noexcept { yL = yR = 0.f; }
    };
}} // namespace ivanna::wfs
