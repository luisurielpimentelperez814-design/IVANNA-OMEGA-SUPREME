#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// hal_clock_producer.hpp — productor experimental de snapshots HAL-CLOCK.
//
// El daemon puede intentar leer SNDRV_PCM_IOCTL_STATUS de un nodo PCM ALSA y
// publicar el par hw_ptr/tstamp devuelto por UNA consulta. Esto no demuestra que
// el nodo sea el stream que Android/HAL tiene activo: el open independiente puede
// fallar por exclusividad o referirse a otro cliente/ruta. Tampoco demuestra por
// sí solo el dominio/unidades del timestamp en el driver concreto.
//
// QUÉ NO ES: no es una integración PAL del stream propietario, ni identifica ni
// acciona un ASRC físico. En el Moto G85 aún faltan callbacks PAL verificados
// para frames+timestamp pareados y para el actuador de corrección de tasa. No
// habilitar corrección física a partir de este productor.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <string>
#include <vector>

namespace ivanna::audio::halclock {

// Estado honesto del productor: la UI y los logs solo muestran esto.
enum class ProducerState : uint8_t {
    kDisabled = 0,     // no habilitado por argumento
    kNoDevices,        // /proc/asound no expone ningún PCM de reproducción
    kNoSource,         // el binario no se compiló con cabeceras ALSA (sin fuente)
    kWaitingSamples,   // dispositivo encontrado, aún sin muestras válidas
    kPublishing,       // publicando muestras correlacionadas
    kRejected,         // muestras presentes pero NO correlacionadas (se descartan)
    kPublishFailed     // el snapshot no se pudo abrir/escribir
};

const char* stateName(ProducerState s) noexcept;

struct PcmDevice {
    int card = -1;
    int device = -1;
    bool capture = false;
    std::string procDir;      // /proc/asound/card0/pcm0p
    std::string devPath;      // /dev/snd/pcmC0D0p
    unsigned nominalRateHz = 0;
    bool running = false;     // true only when /proc/.../status explicitly says state: RUNNING
    std::string state;        // raw state token from /proc/.../status, or diagnostic reason
};

// Muestra pareada: SIEMPRE procede de una única consulta al driver.
struct PairedSample {
    uint64_t presentedFrames = 0;
    int64_t timestampNs = 0;      // CLOCK_MONOTONIC
    uint32_t nominalRateHz = 0;
};

// ── Descubrimiento ALSA conservador ─────────────────────────────────────────
// Primero consulta /proc/asound; si faltan status/hw_params, enumera nodos de
// reproducción en /dev/snd como evidencia de presencia, no como reloj válido.
// El productor no abre ni publica un dispositivo sin rate nominal verificable.
std::vector<PcmDevice> discoverPlaybackDevices(const std::string& procRoot = "/proc/asound",
                                               const std::string& devRoot = "/dev/snd");

// ── Fuente de muestras pareadas ─────────────────────────────────────────────
class PairedSampleSource {
public:
    virtual ~PairedSampleSource() = default;
    virtual const char* name() const noexcept = 0;
    virtual bool open(const PcmDevice& d) noexcept = 0;
    virtual void close() noexcept = 0;
    virtual bool read(PairedSample& out) noexcept = 0;
};

// Implementación con SNDRV_PCM_IOCTL_STATUS. Si el binario no tiene cabeceras
// ALSA (sound/asound.h), open() devuelve false y el productor queda fail-closed.
class StatusIoctlSource final : public PairedSampleSource {
public:
    const char* name() const noexcept override { return "SNDRV_PCM_IOCTL_STATUS"; }
    bool open(const PcmDevice& d) noexcept override;
    void close() noexcept override;
    bool read(PairedSample& out) noexcept override;
    static bool compiledWithAlsaHeaders() noexcept;

private:
    int fd_ = -1;
    uint32_t rate_ = 0;
};

// ── Puerta de correlación: rechaza lo que no es una muestra coherente ───────
struct GateConfig {
    double minRateTolerance = 0.80;   // acepta 0.8×..1.2× del rate nominal
    double maxRateTolerance = 1.20;
    int64_t minDeltaNs = 1000;        // 1 µs: menos es ruido de lectura
    int64_t maxDeltaNs = 500'000'000; // 500 ms: por encima, el PCM estaba parado
    int consecutiveRequired = 3;      // publica tras 3 pares coherentes seguidos
};

struct GateVerdict {
    bool accepted = false;
    const char* reason = "";
    double impliedRateHz = 0.0;
};

class CorrelationGate {
public:
    explicit CorrelationGate(GateConfig c = {}) : cfg_(c) {}
    GateVerdict evaluate(const PairedSample& s) noexcept;
    void reset() noexcept { havePrev_ = false; streak_ = 0; }
    int streak() const noexcept { return streak_; }
    bool readyToPublish() const noexcept { return streak_ >= cfg_.consecutiveRequired; }

private:
    GateConfig cfg_{};
    bool havePrev_ = false;
    PairedSample prev_{};
    int streak_ = 0;
};

// ── Productor completo ─────────────────────────────────────────────────────
struct ProducerStatus {
    ProducerState state = ProducerState::kDisabled;
    const char* reason = "";
    std::string device;
    uint64_t published = 0;
    uint64_t rejected = 0;
    double measuredRateHz = 0.0;
    double driftPpm = 0.0;
    int64_t lastTimestampNs = 0;
};

class HalClockProducer {
public:
    struct Config {
        std::string procRoot = "/proc/asound";
        std::string devRoot = "/dev/snd";
        std::string snapshotPath;  // vacío => kHardwareClockSnapshotPath del bridge
        int pollIntervalMs = 100;
        GateConfig gate{};
        PairedSampleSource* source = nullptr;  // nullptr => StatusIoctlSource
    };

    HalClockProducer() = default;
    ~HalClockProducer();
    explicit HalClockProducer(const Config& c) : cfg_(c) {}

    // Un ciclo: descubrir si hace falta, leer, validar, publicar si procede.
    ProducerStatus poll() noexcept;
    const ProducerStatus& status() const noexcept { return st_; }
    void setEnabled(bool e) noexcept { enabled_ = e; }
    bool enabled() const noexcept { return enabled_; }

private:
    bool ensureSource() noexcept;
    Config cfg_{};
    ProducerStatus st_{};
    CorrelationGate gate_{};
    PcmDevice device_{};
    PairedSampleSource* ownSource_ = nullptr;
    PairedSampleSource* src_ = nullptr;
    bool enabled_ = false;
    bool opened_ = false;
    bool publisherOpen_ = false;
    void* publisher_ = nullptr;  // HardwareClockPublisher* (evita incluir el bridge aquí)
};

}  // namespace ivanna::audio::halclock
