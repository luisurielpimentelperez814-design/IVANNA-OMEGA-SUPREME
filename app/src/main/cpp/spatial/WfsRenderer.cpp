// WfsRenderer.cpp — ver header para la descripción del modelo físico.
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
    speakerBus_.assign(numSpeakers_, 0.f);
    rebuildDelays();
    return true;
}

void WfsRenderer::reset() noexcept {
    for (auto& line : delayLines_)
        for (auto& v : line) v = 0.f;
    for (auto& p : writePos_) p = 0;
    for (auto& v : speakerBus_) v = 0.f;
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
        // Convención de pan corregida (verificación por test: objeto en
        // +x/derecha debe sonar más fuerte y antes en el oído derecho).
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

void WfsRenderer::setObject(int id, float x, float y, float gain) noexcept {
    if (id < 0) return;
    // Clamp de distancia física (una fuente a 1 km rompería la aproximación).
    const float d = std::sqrt(x * x + y * y);
    if (d > kMaxObjectDist) {
        const float sc = kMaxObjectDist / d;
        x *= sc; y *= sc;
    }
    for (auto& o : objects_) {
        if (o.id == id) { o.x = x; o.y = y; o.gain = gain; rebuildDelays(); return; }
    }
    objects_.push_back({id, x, y, gain});
    rebuildDelays();
}

void WfsRenderer::removeObject(int id) noexcept {
    for (size_t i = 0; i < objects_.size(); ++i) {
        if (objects_[i].id == id) {
            objects_.erase(objects_.begin() + static_cast<long>(i));
            rebuildDelays();
            return;
        }
    }
}

void WfsRenderer::rebuildDelays() noexcept {
    const size_t pairs = objects_.size() * static_cast<size_t>(numSpeakers_);
    delayLines_.assign(pairs, std::vector<float>(static_cast<size_t>(maxDelayTap_), 0.f));
    delaySamples_.assign(pairs, 0);
    delayFrac_.assign(pairs, 0.f);
    drivingGain_.assign(pairs, 0.f);
    writePos_.assign(pairs, 0);

    for (size_t oi = 0; oi < objects_.size(); ++oi) {
        const PrimarySource& o = objects_[oi];
        for (int s = 0; s < numSpeakers_; ++s) {
            const size_t idx = oi * static_cast<size_t>(numSpeakers_) + static_cast<size_t>(s);
            // Posición del altavoz (az medido desde atrás → frente = ±π).
            const float sx = -kArrayRadiusM * std::sin(speakerAzimuth_[s]);
            const float sy = -kArrayRadiusM * std::cos(speakerAzimuth_[s]);
            const float dx = o.x - sx, dy = o.y - sy;
            const float d = std::sqrt(dx * dx + dy * dy);
            // Delay fraccionario (interpolación lineal en la lectura).
            const float delaySampF = (d / kSpeedOfSound) * sampleRate_;
            int di = static_cast<int>(delaySampF);
            if (di > maxDelayTap_ - blockSize_ - 4) di = maxDelayTap_ - blockSize_ - 4;
            if (di < 0) di = 0;
            delaySamples_[idx] = di;
            delayFrac_[idx] = delaySampF - static_cast<float>(di);
            // Driving function WFS 2.5D (aprox.): atenuación de onda
            // cilíndrica 1/sqrt(d) referida a kRefDistanceM, por peso de
            // focalización coseno (el altavoz solo contribuye si el objeto
            // cae en su semiespacio frontal).
            const float od = std::sqrt(o.x * o.x + o.y * o.y);
            float focus = 1.f;
            if (od > 1e-6f) {
                // coseno entre dirección al objeto y dirección del altavoz
                const float cosA = (o.x * sx + o.y * sy) / (od * kArrayRadiusM);
                focus = cosA > 0.f ? cosA : 0.f;
            }
            const float att = std::sqrt(kRefDistanceM / (d > kRefDistanceM ? d : kRefDistanceM));
            drivingGain_[idx] = o.gain * att * focus;
        }
    }
}

void WfsRenderer::process(const float* const* objectInputs, int numObjects,
                          float* outL, float* outR, int frames) noexcept {
    if (frames <= 0 || outL == nullptr || outR == nullptr) return;
    if (objectInputs == nullptr || numObjects <= 0 || objects_.empty()) return;
    if (numObjects > static_cast<int>(objects_.size()))
        numObjects = static_cast<int>(objects_.size());

    // 1) Driving: inyectar cada fuente en sus líneas de delay (wrap modular).
    for (int oi = 0; oi < numObjects; ++oi) {
        const float* in = objectInputs[oi];
        if (in == nullptr) continue;
        for (int s = 0; s < numSpeakers_; ++s) {
            const size_t idx = static_cast<size_t>(oi) * static_cast<size_t>(numSpeakers_)
                             + static_cast<size_t>(s);
            auto& line = delayLines_[idx];
            int wp = writePos_[idx];
            for (int n = 0; n < frames; ++n) {
                line[static_cast<size_t>(wp)] = in[n];
                wp = (wp + 1) % maxDelayTap_;
            }
            writePos_[idx] = wp;
        }
    }

    // 2+3) Síntesis del campo y mezcla binaural en UN solo pase por muestra:
    //      cada altavoz emite la suma retardada/ponderada de sus fuentes
    //      (interpolación lineal para el delay fraccionario) y su señal se
    //      encola en su historia ITD; la salida L/R es la suma ILD/ITD del
    //      array completo, normalizada por sqrt(N) para nivel constante.
    const float norm = 1.0f / std::sqrt(static_cast<float>(numSpeakers_));
    for (int n = 0; n < frames; ++n) {
        float accL = 0.f, accR = 0.f;
        for (int s = 0; s < numSpeakers_; ++s) {
            float spk = 0.f;
            for (int oi = 0; oi < numObjects; ++oi) {
                const size_t idx = static_cast<size_t>(oi) * static_cast<size_t>(numSpeakers_)
                                 + static_cast<size_t>(s);
                const auto& line = delayLines_[idx];
                // writePos_ apunta una posición DESPUÉS de la última muestra
                // escrita; la muestra n del bloque vive (frames-1-n) atrás,
                // más el delay físico del par (fuente→altavoz).
                const int back = frames - 1 - n + delaySamples_[idx];
                int rp  = (writePos_[idx] - back) % maxDelayTap_;
                if (rp < 0) rp += maxDelayTap_;
                int rp2 = rp - 1; if (rp2 < 0) rp2 += maxDelayTap_;
                const float a = line[static_cast<size_t>(rp)];
                const float b = line[static_cast<size_t>(rp2)];
                spk += (a + (b - a) * delayFrac_[idx]) * drivingGain_[idx];
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
}

} // namespace ivanna::spatial
