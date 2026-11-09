// RealAcousticSensor.hpp
// ===========================================================================
//  IVANNA — Sensor acústico REAL por micrófono del teléfono (Android 13+).
//  v2.5.369 — el sensor del Hiper-Operador Φ.
//
//  © 2026 Luis Uriel Pimentel Pérez · GORE TNS. All rights reserved.
//
//  ALCANCE: IVANNA es para CUALQUIER Android 13 en adelante. El Moto G85
//  es el dispositivo de desarrollo/prueba de referencia, pero este sensor
//  no tiene ningún supuesto específico del G85 hardcodeado: usa AudioRecord
//  genérico (UNPROCESSED con fallback a MIC, 48 kHz mono, PCM_FLOAT) y la
//  frecuencia de muestreo se configura en prepare(). Cualquier teléfono
//  Android 13+ con micrófono es el sensor.
//
//  QUÉ ES: el teléfono mismo es el sensor. Mientras suena música por el
//  altavoz del teléfono o por la salida Aux (Genezi / auto), el micrófono
//  del teléfono captura la salida acústica REAL de los altavoces en la
//  habitación / cabina. Este componente mide, en tiempo real y por banda
//  ERB (24 bandas, las mismas que HOLOS), la transferencia real
//  altavoz+habitación: magnitud medida, error contra el objetivo y
//  coherencia micrófono/referencia. Nada simulado: cada número sale de
//  muestras PCM reales del micrófono.
//
//  CÓMO MIDE (por frame de análisis, FFT 8192, hop 4096 = 50% de
//  solapamiento, ~85 ms por frame @48 kHz):
//    1. Acumula PCM del micrófono (AudioRecord, hilo no-RT) y PCM de
//       referencia — la mezcla que sale del altavoz, empujada por el hilo
//       de audio de la Ruta A (nativeProcessBlock) a un anillo SPSC
//       lock-free, sin mutex ni bloqueo en audio. Ventana Hann, FFT
//       radix-2 (la del repo). La FFT 8192 (~5.9 Hz/bin @48 kHz) duplica
//       la resolución en graves frente a la 4096 (~11.7 Hz/bin): la banda
//       ERB más grave (60–113 Hz) pasa de ~4–5 bins a ~9 bins, y el
//       solapamiento del 50% mantiene la tasa de frames en ~85 ms.
//    2. Por banda ERB: potencia media (normalización exacta: un seno de
//       amplitud A da 10·log10(A²/2) dBFS, verificado en test), suavizado
//       exponencial ~250 ms, y espectro cruzado mic×ref para coherencia.
//    3. magnitude_db[b]: nivel medido por banda (dBFS RMS).
//       error_db[b] = (magnitude_db[b] − nivelRef) − objetivo[b], donde
//       nivelRef es la media de magnitude_db en 250 Hz–2 kHz (como
//       RoomProbe): el error es el BALANCE TONAL, independiente del
//       volumen general. objetivo por defecto: Harman AE/OE 2018
//       (EliteTargetCurves), el único target con respaldo experimental
//       sólido (Olive/Welti/Khonsaripour 2018, r=0.86).
//       coherence[b] = |Sxy|²/(Sxx·Syy) por banda [0..1]: 1 = el mic
//       escucha exactamente la reproducción; 0 = no hay referencia o
//       el mic escucha otra cosa (ruido, voz, silencio).
//    4. Publica un SensorFrame por análisis. La publicación es lock-free
//       (seqlock): el hilo de audio / el operador lee latestFrame() sin
//       bloquear jamás.
//
//  HILOS: feedMic/feedReference/setTarget/reset corren en hilos no-RT
//  (captura AudioRecord y referencia de reproducción); se serializan con
//  un mutex interno. latestFrame() es lock-free y RT-safe. El sink
//  (setSink) se invoca en el hilo del sensor, FUERA del mutex, con una
//  copia del frame: el receptor solo debe guardar el frame, nunca hacer
//  DSP dentro del sink.
//
//  FAIL-CLOSED (nada inventado):
//    - Sin datos suficientes, con nivel bajo silencio (< −70 dBFS) o con
//      muestras no finitas: el frame se publica con valid=false y el
//      estado suavizado NO se corrompe (un frame inválido no toca el
//      estado). El operador debe ignorar frames con valid=false.
//    - Sin referencia alimentada: coherence = 0 en todas las bandas
//      (no se finge coherencia). magnitude/error siguen siendo reales:
//      la medición principal no necesita la referencia. En la Ruta A la
//      referencia la empuja el hilo de audio (la salida real al altavoz),
//      así que en uso normal SÍ hay referencia y la coherencia es real.
//    - amount/aplicación de la corrección vive en el operador / en el
//      bucle de Ruta A, no aquí: este componente solo MIDE. El método
//      mapErrorToRoomCorrection traduce error_db ERB → corrección
//      RoomProbe (10 bandas) para la etapa RoomCorrectionEq de Ruta A.
//
//  LIMITACIONES HONESTAS (ver docs/REAL_ACOUSTIC_SENSOR.md):
//    - El micrófono del teléfono NO está calibrado: mide balance tonal
//      relativo, no nivel absoluto SPL.
//    - Mide el punto donde está el teléfono, no la posición de los oídos
//      del oyente. Si el teléfono está en el bolsillo o lejos de los
//      altavoces, la medición es la de ese punto.
//    - Resolución en graves limitada por la FFT 8192 (~5.9 Hz/bin @48 kHz)
//      y por el propio micrófono (poco fiable bajo ~80 Hz).
//    - La corrección es LENTA (por frame de ~85 ms + suavizado 250 ms):
//      es una medida de sala, no una corrección muestra a muestra.
//    - La coherencia exige que la referencia esté aproximadamente
//      alineada en el tiempo con el micrófono (dentro de una ventana).
//      Con retardo grande o contenido no estacionario, la coherencia
//      baja aunque la magnitud siga siendo válida.
// ===========================================================================
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

#include "EliteTargetCurves.hpp"
#include "RoomProbe.hpp"
#include "fft_radix2.hpp"

namespace ivanna::spatial {

// ── SensorFrame: el contrato exacto que consume HyperStateOperator ────────
//    HyperStateOperator::setSensorFrame(const SensorFrame&) recibe este
//    struct (incluir este header). Las 24 bandas son las mismas 24 bandas
//    ERB de HOLOS (60 Hz–16 kHz, escala ERB-rate), en el mismo orden.
struct SensorFrame {
    static constexpr int kBands = 24;
    float magnitude_db[kBands] = {};  // nivel medido por banda ERB (dBFS RMS)
    float error_db[kBands] = {};      // (magnitud − nivelRef) − objetivo (dB)
    float coherence[kBands] = {};     // coherencia mic/referencia [0..1]
    float level_dbfs = -120.0f;       // nivel de banda ancha del mic (dBFS RMS)
    uint64_t frame_index = 0;         // contador de frames publicados
    bool valid = false;               // true = medición confiable, usarla
};

// Escala ERB-rate, IDÉNTICA a HOLOS (HolosOperator::rebuildBandTables):
// bordes 60 Hz – min(16000, 0.45·sr), 24 bandas, centro = media geométrica.
inline float sensorErbRate(float fHz) noexcept {
    return 21.4f * std::log10(1.0f + 0.00437f * fHz);
}
inline float sensorErbInverse(float rate) noexcept {
    return (std::pow(10.0f, rate / 21.4f) - 1.0f) / 0.00437f;
}

class RealAcousticSensor {
public:
    // FFT 8192 (~5.9 Hz/bin @48 kHz): el doble de resolución en graves
    // que la 4096. Hop 4096 = 50% de solapamiento: un frame cada ~85 ms.
    static constexpr int kFftSize = 8192;
    static constexpr int kHopSize = 4096;
    static constexpr int kBands = 24;
    // Anillo SPSC de referencia: 16384 muestras = 341 ms @48 kHz.
    static constexpr std::size_t kRefRingSize = 16384;
    static constexpr std::size_t kRefRingMask = kRefRingSize - 1;
    // Nivel por debajo del cual la medición es silencio (no confiable).
    static constexpr float kMinLevelDbfs = -70.0f;
    // Piso de magnitud (dBFS) para log10 de potencias diminutas.
    static constexpr float kMagFloorDb = -120.0f;
    // Constante de tiempo del suavizado de potencias (~250 ms).
    static constexpr float kSmoothTauSec = 0.25f;

    RealAcousticSensor() { prepare(48000.0f); }

    // (Re)configura para una frecuencia de muestreo. Resetea el estado.
    void prepare(float sampleRate) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        const float sr = (std::isfinite(sampleRate) && sampleRate >= 8000.0f &&
                          sampleRate <= 192000.0f)
                             ? sampleRate
                             : 48000.0f;
        sampleRate_ = sr;
        fft_ = FFTRadix2(kFftSize);
        window_.assign(kFftSize, 0.0f);
        constexpr float kPi = 3.14159265358979323846f;
        for (int i = 0; i < kFftSize; ++i)
            window_[i] = 0.5f - 0.5f * std::cos(2.0f * kPi * (float)i / (float)kFftSize);
        // Bandas ERB (mismas que HOLOS).
        const float fTop = std::min(16000.0f, 0.45f * sr);
        const float rLo = sensorErbRate(60.0f);
        const float rHi = sensorErbRate(fTop);
        float edgeHz[kBands + 1];
        for (int i = 0; i <= kBands; ++i)
            edgeHz[i] = sensorErbInverse(rLo + (rHi - rLo) * (float)i / (float)kBands);
        for (int b = 0; b < kBands; ++b) {
            const float lo = edgeHz[b], hi = edgeHz[b + 1];
            centerHz_[b] = std::sqrt(lo * hi);
            loBin_[b] = std::max(0, (int)std::ceil(lo * (float)kFftSize / sr));
            hiBin_[b] = std::min(kFftSize / 2, (int)std::floor(hi * (float)kFftSize / sr));
            if (hiBin_[b] <= loBin_[b]) hiBin_[b] = loBin_[b] + 1;
        }
        // Target por defecto: el preset élite elegido (Harman AE/OE 2018
        // salvo que setEliteTarget/setTarget haya fijado otro). Un cambio
        // de sample rate no debe borrar la curva que eligió el usuario:
        // se reconstruye desde targetKind_ + bassTrimDb_ (o se conserva
        // la personalizada si targetCustom_).
        if (!targetCustom_) {
            float curve[kBands];
            buildEliteTarget24(targetKind_, centerHz_, curve);
            applyBassTrimDb(curve, centerHz_, bassTrimDb_);
            for (int b = 0; b < kBands; ++b) targetDb_[b] = curve[b];
        }
        // Bandas de referencia (250 Hz–2 kHz) para el nivelRef.
        midCount_ = 0;
        for (int b = 0; b < kBands; ++b)
            if (centerHz_[b] >= 250.0f && centerHz_[b] <= 2000.0f)
                midBands_[midCount_++] = b;
        if (midCount_ == 0) {  // no debería pasar; respaldo: todas
            for (int b = 0; b < kBands; ++b) midBands_[midCount_++] = b;
        }
        // El suavizado corre por análisis (uno por hop), no por FFT:
        // alfa = 1 − exp(−hopSec/τ) con hopSec = 4096/sr (~85 ms).
        const float hopSec = (float)kHopSize / sr;
        smoothAlpha_ = 1.0f - std::exp(-hopSec / kSmoothTauSec);
        resetLocked();
    }

    // Objetivo por banda (forma deseada, dB). 24 valores.
    // Default: Harman AE/OE 2018 (EliteTargetCurves) — el único target con
    // respaldo experimental sólido (Olive/Welti/Khonsaripour 2018, r=0.86).
    // REGLA DE PROPIETARIO ÚNICO: el target del modo MEDIDO vive AQUÍ
    // (error_db ya lo incluye); el operador NO lo reaplica. El JNI
    // nativeSetHyperTarget aplica la misma curva aquí y en el operador
    // (modo APROXIMADO) para que ambos corrijan hacia la misma referencia.
    // Valores no finitos se ignoran (la banda conserva su objetivo).
    void setTarget(const float* targetDb24) noexcept {
        if (targetDb24 == nullptr) return;
        std::lock_guard<std::mutex> lock(mutex_);
        for (int b = 0; b < kBands; ++b) {
            const float t = targetDb24[b];
            if (std::isfinite(t)) targetDb_[b] = std::clamp(t, -30.0f, 30.0f);
        }
        targetCustom_ = true;
    }
    float targetDb(int b) const noexcept {
        if (b < 0 || b >= kBands) return 0.0f;
        std::lock_guard<std::mutex> lock(mutex_);
        return targetDb_[b];
    }

    // setEliteTarget: elige la curva objetivo por preset + control de
    // graves. Espejo exacto de HyperStateOperator::setEliteTarget, para
    // que un solo JNI (nativeSetHyperTarget) pueda fijar la misma curva
    // en el sensor (modo MEDIDO) y en el operador (modo APROXIMADO) sin
    // divergencia ni doble aplicación (regla de propietario único).
    //   kind: 0 Harman 2018 (default), 1 Harman élite-lean, 2 campo
    //   difuso H&M, 3 altavoces en sala. Kind inválido → Harman 2018.
    //   bassTrimDb: ±3 dB (estante suave <100 Hz, preferencia individual
    //   de graves, Olive 2025). Construye la curva de 24 bandas sobre
    //   los centros ERB actuales y la aplica como target.
    void setEliteTarget(EliteTargetKind kind, float bassTrimDb) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        setEliteTargetLocked(kind, bassTrimDb);
    }
    void setBassTrimDb(float bassTrimDb) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        setEliteTargetLocked(targetKind_, bassTrimDb);
    }
    EliteTargetKind targetKind() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return targetKind_;
    }
    float bassTrimDb() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return bassTrimDb_;
    }

    void setEnabled(bool enabled) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        enabled_ = enabled;
        if (!enabled_) resetLocked();
    }
    bool isEnabled() const noexcept { return enabled_; }

    void reset() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        resetLocked();
    }

    float sampleRate() const noexcept { return sampleRate_; }
    float bandCenterHz(int b) const noexcept {
        if (b < 0 || b >= kBands) return 0.0f;
        return centerHz_[b];
    }
    // Nivel de referencia (media de magnitude_db en 250 Hz–2 kHz).
    float referenceLevelDb() const noexcept { return refLevelDb_; }
    uint64_t framesPublished() const noexcept {
        return frameIndex_.load(std::memory_order_relaxed);
    }

    // Sink: se invoca (hilo del sensor, fuera del mutex) con cada frame
    // publicado. El receptor solo guarda el frame; nunca hace DSP aquí.
    void setSink(std::function<void(const SensorFrame&)> sink) {
        std::lock_guard<std::mutex> lock(mutex_);
        sink_ = std::move(sink);
    }

    // Alimenta PCM del micrófono (float mono, [-1,1]). Hilo no-RT.
    // Devuelve false si se rechaza (apagado, puntero nulo, o >50% de
    // muestras no finitas). Las muestras no finitas aisladas se ponen a 0.
    bool feedMic(const float* pcm, std::size_t n) noexcept {
        if (pcm == nullptr || n == 0) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!enabled_) return false;
        std::size_t bad = 0;
        for (std::size_t i = 0; i < n; ++i)
            if (!std::isfinite(pcm[i])) ++bad;
        if (bad * 2 > n) return false;  // >50% basura: rechazar el bloque
        // Anillo de micrófono (8192 muestras, potencia de 2): se escribe
        // en (micWritePos_ + i) & (kFftSize − 1). Cada hop (4096 muestras
        // nuevas) se analiza la ventana más reciente de 8192 muestras
        // (50% de solapamiento): un frame cada ~85 ms @48 kHz, con el
        // doble de resolución en graves que la FFT 4096.
        constexpr std::size_t kMask = (std::size_t)(kFftSize - 1);
        for (std::size_t i = 0; i < n; ++i) {
            const float s = pcm[i];
            micRing_[(micWritePos_ + i) & kMask] = std::isfinite(s) ? s : 0.0f;
        }
        micWritePos_ += n;
        while (micWritePos_ - micAnalyzedPos_ >= (std::size_t)kHopSize &&
               micWritePos_ >= (std::size_t)kFftSize) {
            // Copiar la ventana más reciente (8192) al buffer de trabajo.
            const std::size_t start = micWritePos_ - (std::size_t)kFftSize;
            for (int i = 0; i < kFftSize; ++i)
                micWork_[i] = micRing_[(start + (std::size_t)i) & kMask];
            analyzeLocked();
            micAnalyzedPos_ += (std::size_t)kHopSize;
        }
        return true;
    }

    // ── Referencia (anillo SPSC lock-free, productor = hilo de audio) ─────
    // pushReference: la salida real al altavoz (mono, float [-1,1]).
    // La llama nativeProcessBlock (Ruta A) con la mezcla ya corregida y
    // limitada, es decir, EXACTAMENTE lo que el micrófono escucha.
    // Lock-free, nunca bloquea, nunca toma el mutex: si el anillo está
    // lleno se descartan las muestras nuevas (el consumidor salta a lo
    // más reciente). Sin referencia, coherence = 0 (no se finge).
    // NO hay feedReference por JNI/Kotlin: la referencia es la salida
    // nativa de la Ruta A, y un segundo productor rompería el SPSC.
    bool pushReference(const float* pcm, std::size_t n) noexcept {
        if (pcm == nullptr || n == 0) return false;
        const std::size_t tail = refTail_.load(std::memory_order_acquire);
        std::size_t head = refHead_.load(std::memory_order_relaxed);
        for (std::size_t i = 0; i < n; ++i) {
            if (head - tail >= kRefRingSize) break;  // lleno: descartar
            const float s = pcm[i];
            refRingSpsc_[head & kRefRingMask] = std::isfinite(s) ? s : 0.0f;
            ++head;
        }
        refHead_.store(head, std::memory_order_release);
        return true;
    }

    // Mapea error_db ERB (24) → corrección RoomProbe (10 bandas, dB).
    // correction = clamp(−error · amount, −9, +3): se invierte la
    // desviación medida (un boom medido se recorta, un valle se realza
    // poco). amount 0..1 (0 = sin corrección). Cada banda RoomProbe
    // promedia el error de las bandas ERB dentro de su octava.
    // Requiere que el sensor esté preparado (lo está desde el constructor).
    void mapErrorToRoomCorrection(const float* erbErrorDb24, float amount,
                                  float* roomCorrDb10) const noexcept {
        if (erbErrorDb24 == nullptr || roomCorrDb10 == nullptr) return;
        const float amt = std::isfinite(amount) ? std::clamp(amount, 0.0f, 1.0f) : 0.0f;
        for (int rb = 0; rb < room::kBands; ++rb) {
            const float fc = room::kBandHz[rb];
            const float lo = fc * 0.70710678f, hi = fc * 1.41421356f;
            double sum = 0.0;
            int cnt = 0;
            int nearest = 0;
            float nearestDist = 1e30f;
            for (int b = 0; b < kBands; ++b) {
                const float d = std::fabs(std::log2(centerHz_[b] / fc));
                if (d < nearestDist) { nearestDist = d; nearest = b; }
                if (centerHz_[b] >= lo && centerHz_[b] <= hi) {
                    sum += (double)erbErrorDb24[b];
                    ++cnt;
                }
            }
            const float err = (cnt > 0) ? (float)(sum / (double)cnt) : erbErrorDb24[nearest];
            const float corr = std::isfinite(err) ? -err * amt : 0.0f;
            // Filosofía RoomProbe: recortar hasta −9 dB, realzar hasta +3 dB
            // (un valle de sala no se rellena con ganancia).
            roomCorrDb10[rb] = std::clamp(corr, -9.0f, 3.0f);
        }
    }

    // Lectura lock-free (seqlock) para el hilo de audio / operador.
    // Nunca bloquea. Si no se logra una copia consistente tras 4
    // intentos, devuelve el frame con valid=false (no usar).
    SensorFrame latestFrame() const noexcept {
        for (int attempt = 0; attempt < 4; ++attempt) {
            const uint64_t s0 = slot_.seq.load(std::memory_order_acquire);
            if (s0 & 1u) continue;  // publicando: reintentar
            SensorFrame f = slot_.frame;
            const uint64_t s1 = slot_.seq.load(std::memory_order_acquire);
            if (s0 == s1) return f;
        }
        SensorFrame f = slot_.frame;
        f.valid = false;
        return f;
    }

private:
    // Aplica un preset élite + trim. Requiere el mutex tomado.
    // (setEliteTarget/setBassTrimDb son los puntos de entrada públicos.)
    void setEliteTargetLocked(EliteTargetKind kind, float bassTrimDb) noexcept {
        const int k = static_cast<int>(kind);
        targetKind_ = (k >= 0 && k < kEliteTargetKindCount)
                          ? kind
                          : EliteTargetKind::Harman2018;
        bassTrimDb_ = std::isfinite(bassTrimDb)
                          ? std::clamp(bassTrimDb, -3.0f, 3.0f)
                          : 0.0f;
        float curve[kBands];
        buildEliteTarget24(targetKind_, centerHz_, curve);
        applyBassTrimDb(curve, centerHz_, bassTrimDb_);
        for (int b = 0; b < kBands; ++b) targetDb_[b] = curve[b];
        targetCustom_ = false;  // es un preset, no una curva a mano
    }

    void resetLocked() noexcept {
        micRing_.fill(0.0f);
        micWork_.fill(0.0f);
        micWritePos_ = 0;
        micAnalyzedPos_ = 0;
        refRingSpsc_.fill(0.0f);
        refHead_.store(0, std::memory_order_relaxed);
        refTail_.store(0, std::memory_order_relaxed);
        sxxSm_.fill(0.0f);
        syySm_.fill(0.0f);
        sxyReSm_.fill(0.0f);
        sxyImSm_.fill(0.0f);
        hasSmooth_ = false;
        refLevelDb_ = kMagFloorDb;
        frameIndex_.store(0, std::memory_order_relaxed);
        SensorFrame empty{};
        publish(empty);
    }

    void publish(const SensorFrame& f) noexcept {
        slot_.seq.fetch_add(1, std::memory_order_relaxed);  // → impar
        slot_.frame = f;
        slot_.seq.fetch_add(1, std::memory_order_release);  // → par
    }

    // Un frame de análisis completo (ventana de 8192 muestras de mic,
    // la más reciente). Corre con el mutex tomado, en el hilo del sensor.
    // Nunca toca el hilo de audio.
    void analyzeLocked() noexcept {
        // RMS de banda ancha del mic (dominio temporal, sin ventana).
        double sumSq = 0.0;
        for (int i = 0; i < kFftSize; ++i) {
            const double s = (double)micWork_[i];
            sumSq += s * s;
        }
        const double rms = std::sqrt(sumSq / (double)kFftSize);
        const float levelDb = (rms > 0.0)
                                  ? (float)(20.0 * std::log10(rms))
                                  : kMagFloorDb;
        const uint64_t idx = frameIndex_.load(std::memory_order_relaxed) + 1;
        frameIndex_.store(idx, std::memory_order_relaxed);

        // Referencia fresca: drenar el anillo SPSC (lo más reciente).
        // Si hay menos de un frame completo, no hay referencia útil.
        std::vector<float> reR(kFftSize, 0.0f);
        const bool refFresh = drainReference(reR.data());
        // (reR queda en ceros si no hay referencia fresca.)

        SensorFrame out{};
        out.frame_index = idx;
        out.level_dbfs = std::max(levelDb, kMagFloorDb);

        if (levelDb <= kMinLevelDbfs) {
            // Silencio / nivel inútil: no corromper el estado suavizado.
            // Publicar valid=false con los últimos valores conocidos.
            out.valid = false;
            const SensorFrame prev = slot_.frame;
            for (int b = 0; b < kBands; ++b) {
                out.magnitude_db[b] = prev.magnitude_db[b];
                out.error_db[b] = prev.error_db[b];
                out.coherence[b] = 0.0f;
            }
            publish(out);
            if (sink_) sink_(out);
            return;
        }

        // FFT del mic (ventana Hann) y de la referencia (ya drenada).
        std::vector<float> reM(kFftSize), imM(kFftSize, 0.0f);
        std::vector<float> imR(kFftSize, 0.0f);
        for (int i = 0; i < kFftSize; ++i) {
            reM[i] = micWork_[i] * window_[i];
            if (refFresh) reR[i] *= window_[i];
        }
        fft_.forward(reM.data(), imM.data());
        if (refFresh) fft_.forward(reR.data(), imR.data());

        // Normalización exacta: P_banda = (2/(N²·mean(w²)))·Σ|X[k]|².
        // mean(w²) Hann = 3/8. Un seno de amplitud A da P = A²/2.
        constexpr float kMeanW2 = 0.375f;  // media de Hann²
        const float norm = 2.0f / ((float)kFftSize * (float)kFftSize * kMeanW2);
        const float alpha = hasSmooth_ ? smoothAlpha_ : 1.0f;
        for (int b = 0; b < kBands; ++b) {
            double sxx = 0.0, syy = 0.0, sxyRe = 0.0, sxyIm = 0.0;
            for (int k = loBin_[b]; k < hiBin_[b]; ++k) {
                const double mr = reM[k], mi = imM[k];
                sxx += mr * mr + mi * mi;
                if (refFresh) {
                    const double rr = reR[k], ri = imR[k];
                    syy += rr * rr + ri * ri;
                    sxyRe += mr * rr + mi * ri;
                    sxyIm += mi * rr - mr * ri;
                }
            }
            const float pxx = (float)(sxx * (double)norm);
            const float pyy = refFresh ? (float)(syy * (double)norm) : 0.0f;
            const float pxyRe = refFresh ? (float)(sxyRe * (double)norm) : 0.0f;
            const float pxyIm = refFresh ? (float)(sxyIm * (double)norm) : 0.0f;
            sxxSm_[b] += alpha * (pxx - sxxSm_[b]);
            syySm_[b] += alpha * (pyy - syySm_[b]);
            sxyReSm_[b] += alpha * (pxyRe - sxyReSm_[b]);
            sxyImSm_[b] += alpha * (pxyIm - sxyImSm_[b]);
        }
        hasSmooth_ = true;

        // Magnitud (dBFS) y nivel de referencia (media 250 Hz–2 kHz).
        float magDb[kBands];
        double refSum = 0.0;
        for (int b = 0; b < kBands; ++b) {
            magDb[b] = 10.0f * std::log10(std::max(sxxSm_[b], 1e-12f));
            if (magDb[b] < kMagFloorDb) magDb[b] = kMagFloorDb;
            out.magnitude_db[b] = magDb[b];
        }
        for (int i = 0; i < midCount_; ++i) refSum += (double)magDb[midBands_[i]];
        refLevelDb_ = (float)(refSum / (double)midCount_);
        for (int b = 0; b < kBands; ++b) {
            out.error_db[b] = (magDb[b] - refLevelDb_) - targetDb_[b];
            const float denom = sxxSm_[b] * syySm_[b];
            float coh = 0.0f;
            if (refFresh && denom > 0.0f) {
                const float num = sxyReSm_[b] * sxyReSm_[b] + sxyImSm_[b] * sxyImSm_[b];
                coh = num / denom;
            }
            out.coherence[b] = std::clamp(coh, 0.0f, 1.0f);
        }
        out.valid = true;
        publish(out);
        if (sink_) sink_(out);
    }

    // ── Configuración ──────────────────────────────────────────────────────
    float sampleRate_ = 48000.0f;
    float targetDb_[kBands] = {};
    bool targetCustom_ = false;  // true = setTarget() fijó una curva propia
    EliteTargetKind targetKind_ = EliteTargetKind::Harman2018;
    float bassTrimDb_ = 0.0f;    // control de graves ±3 dB (Olive 2025)
    bool enabled_ = true;
    float smoothAlpha_ = 0.29f;

    // ── Bandas ERB ─────────────────────────────────────────────────────────
    int loBin_[kBands] = {};
    int hiBin_[kBands] = {};
    float centerHz_[kBands] = {};
    int midBands_[kBands] = {};
    int midCount_ = 0;

    // ── Estado de análisis (hilo del sensor, bajo mutex) ──────────────────
    FFTRadix2 fft_{kFftSize};
    std::vector<float> window_;
    // Anillo de micrófono (últimas 8192 muestras) + buffer de trabajo
    // (la ventana que se analiza). Contadores monótonos: la posición es
    // counter & (kFftSize − 1) (potencia de 2).
    std::array<float, kFftSize> micRing_{};
    std::array<float, kFftSize> micWork_{};
    std::size_t micWritePos_ = 0;
    std::size_t micAnalyzedPos_ = 0;
    std::array<float, kBands> sxxSm_{};
    std::array<float, kBands> syySm_{};
    std::array<float, kBands> sxyReSm_{};
    std::array<float, kBands> sxyImSm_{};
    bool hasSmooth_ = false;
    float refLevelDb_ = kMagFloorDb;
    std::function<void(const SensorFrame&)> sink_;
    mutable std::mutex mutex_;

    // ── Anillo SPSC de referencia (productor = hilo de audio) ─────────────
    // 16384 muestras = 341 ms @48 kHz. Solo el hilo de audio escribe
    // refHead_; solo el hilo del sensor escribe refTail_. Contadores
    // monótonos; el índice es counter & máscara (potencia de 2).
    std::array<float, kRefRingSize> refRingSpsc_{};
    std::atomic<std::size_t> refHead_{0};
    std::atomic<std::size_t> refTail_{0};

    // Drena las ÚLTIMAS kFftSize muestras de referencia al buffer out.
    // Si hay menos de un frame completo, devuelve false (sin referencia
    // útil) y deja out en ceros. Corre en el hilo del sensor, bajo mutex.
    bool drainReference(float* out) noexcept {
        const std::size_t head = refHead_.load(std::memory_order_acquire);
        std::size_t tail = refTail_.load(std::memory_order_relaxed);
        const std::size_t avail = head - tail;
        if (avail < (std::size_t)kFftSize) return false;
        // Saltar lo viejo: quedarse con las últimas kFftSize muestras.
        tail = head - (std::size_t)kFftSize;
        for (int i = 0; i < kFftSize; ++i)
            out[i] = refRingSpsc_[(tail + (std::size_t)i) & kRefRingMask];
        // Ventana deslizante: la ventana es de kFftSize (8192) pero el
        // análisis corre cada hop (4096). Si se consumieran las 8192,
        // al siguiente análisis solo habrían llegado 4096 nuevas y la
        // referencia no estaría fresca (coherencia ~0.5). Se conserva el
        // solapamiento (kFftSize − kHopSize = 4096 muestras) para el
        // próximo análisis: se consumen solo kHopSize por análisis, el
        // mismo ritmo al que llega la referencia.
        refTail_.store(head - (std::size_t)(kFftSize - kHopSize),
                       std::memory_order_release);
        return true;
    }

    // ── Publicación lock-free (seqlock) ────────────────────────────────────
    struct PublishSlot {
        std::atomic<uint64_t> seq{0};
        SensorFrame frame{};
    };
    mutable PublishSlot slot_;
    mutable std::atomic<uint64_t> frameIndex_{0};
};

}  // namespace ivanna::spatial
