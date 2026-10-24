#include "hal_clock_producer.hpp"

#include "../include/audio_hardware_clock_bridge.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>

#if defined(__has_include)
#  if __has_include(<sound/asound.h>)
#    include <sound/asound.h>
#    define IVANNA_HAS_ASOUND 1
#  endif
#endif
#ifndef IVANNA_HAS_ASOUND
#  define IVANNA_HAS_ASOUND 0
#endif

namespace ivanna::audio::halclock {
namespace {

std::string readSmallFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Extrae el primer entero que sigue a `key` en un archivo de /proc/asound.
bool parseIntAfter(const std::string& text, const char* key, long& out) {
    const std::string k(key);
    size_t p = text.find(k);
    if (p == std::string::npos) return false;
    p += k.size();
    while (p < text.size() && (text[p] == ' ' || text[p] == '\t' || text[p] == ':')) ++p;
    char* end = nullptr;
    const long v = std::strtol(text.c_str() + p, &end, 10);
    if (end == text.c_str() + p) return false;
    out = v;
    return true;
}

}  // namespace

const char* stateName(ProducerState s) noexcept {
    switch (s) {
        case ProducerState::kDisabled:      return "disabled";
        case ProducerState::kNoDevices:     return "no-devices";
        case ProducerState::kNoSource:      return "no-source";
        case ProducerState::kWaitingSamples:return "waiting-samples";
        case ProducerState::kPublishing:    return "publishing";
        case ProducerState::kRejected:      return "rejected";
        case ProducerState::kPublishFailed: return "publish-failed";
    }
    return "unknown";
}

std::vector<PcmDevice> discoverPlaybackDevices(const std::string& procRoot,
                                               const std::string& devRoot) {
    std::vector<PcmDevice> out;
    for (int card = 0; card < 16; ++card) {
        const std::string cardDir = procRoot + "/card" + std::to_string(card);
        if (::access(cardDir.c_str(), F_OK) != 0) continue;
        for (int pcm = 0; pcm < 32; ++pcm) {
            for (int sub = 0; sub < 2; ++sub) {
                const std::string pdir = cardDir + "/pcm" + std::to_string(pcm) + "p/sub" +
                                         std::to_string(sub);
                const std::string statusPath = pdir + "/status";
                if (::access(statusPath.c_str(), F_OK) != 0) continue;

                PcmDevice d;
                d.card = card;
                d.device = pcm;
                d.capture = false;  // pcmNp => playback
                d.procDir = pdir;
                d.devPath = devRoot + "/pcmC" + std::to_string(card) + "D" +
                            std::to_string(pcm) + "p";
                const std::string status = readSmallFile(statusPath);
                long stateVal = 0;
                if (parseIntAfter(status, "state", stateVal))
                    d.state = std::to_string(stateVal);
                else
                    d.state = status.empty() ? "unknown" : "parsed-empty";

                const std::string hw = readSmallFile(pdir + "/hw_params");
                long rate = 0;
                if (parseIntAfter(hw, "rate", rate) && rate > 0) {
                    d.nominalRateHz = static_cast<unsigned>(rate);
                } else {
                    // Sin rate no hay correlación posible: no se publica.
                    d.nominalRateHz = 0;
                }
                out.push_back(d);
            }
        }
    }
    return out;
}

bool StatusIoctlSource::compiledWithAlsaHeaders() noexcept { return IVANNA_HAS_ASOUND != 0; }

bool StatusIoctlSource::open(const PcmDevice& d) noexcept {
    close();
#if IVANNA_HAS_ASOUND
    if (d.device < 0 || d.card < 0) return false;
    fd_ = ::open(d.devPath.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd_ < 0) return false;
    rate_ = d.nominalRateHz;
    return true;
#else
    (void)d;
    return false;
#endif
}

void StatusIoctlSource::close() noexcept {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
    rate_ = 0;
}

bool StatusIoctlSource::read(PairedSample& out) noexcept {
#if IVANNA_HAS_ASOUND
    if (fd_ < 0) return false;
    struct snd_pcm_status st{};
    if (::ioctl(fd_, SNDRV_PCM_IOCTL_STATUS, &st) < 0) return false;
    // Una sola consulta => un solo par (hw_ptr, tstamp). Nunca se mezclan.
    out.presentedFrames = static_cast<uint64_t>(st.hw_ptr);
    out.timestampNs = static_cast<int64_t>(st.tstamp.tv_sec) * 1000000000ll +
                      static_cast<int64_t>(st.tstamp.tv_nsec);
    out.nominalRateHz = rate_;
    return true;
#else
    (void)out;
    return false;
#endif
}

GateVerdict CorrelationGate::evaluate(const PairedSample& s) noexcept {
    GateVerdict v{};
    if (s.nominalRateHz == 0) { v.reason = "sin rate nominal"; return v; }
    if (s.timestampNs <= 0) { v.reason = "timestamp no positivo"; return v; }

    if (!havePrev_) {
        // Primera muestra: solo se acepta como ancla, no publica todavía.
        prev_ = s;
        havePrev_ = true;
        streak_ = 0;
        v.accepted = false;
        v.reason = "primera muestra: ancla";
        return v;
    }

    const int64_t dNs = s.timestampNs - prev_.timestampNs;
    if (dNs <= cfg_.minDeltaNs) { v.reason = "delta de tiempo no positivo o nulo"; goto reject; }
    if (dNs > cfg_.maxDeltaNs)  { v.reason = "delta de tiempo excesivo (PCM parado)"; goto reject; }
    if (s.presentedFrames < prev_.presentedFrames) {
        // El contador de ALSA puede envolver, pero un retroceso pequeño es siempre error.
        const uint64_t back = prev_.presentedFrames - s.presentedFrames;
        if (back < (1ull << 63)) { v.reason = "frames retroceden"; goto reject; }
    }
    {
        const double dFrames = static_cast<double>(s.presentedFrames - prev_.presentedFrames);
        const double dSec = static_cast<double>(dNs) / 1e9;
        v.impliedRateHz = dFrames / dSec;
        const double nominal = static_cast<double>(s.nominalRateHz);
        if (v.impliedRateHz < nominal * cfg_.minRateTolerance ||
            v.impliedRateHz > nominal * cfg_.maxRateTolerance) {
            v.reason = "tasa implicita fuera de tolerancia";
            goto reject;
        }
    }

    prev_ = s;
    ++streak_;
    v.accepted = true;
    v.reason = "correlacionada";
    return v;

reject:
    prev_ = s;   // la referencia avanza para no bloquear indefinidamente
    streak_ = 0;
    v.accepted = false;
    return v;
}

HalClockProducer::~HalClockProducer() {
    delete ownSource_;
    if (publisher_) {
        auto* p = static_cast<ivanna::audio::HardwareClockPublisher*>(publisher_);
        p->close();
        delete p;
    }
}

bool HalClockProducer::ensureSource() noexcept {
    if (src_) return true;
    if (cfg_.source) { src_ = cfg_.source; return true; }
    auto* s = new StatusIoctlSource();
    ownSource_ = s;
    src_ = s;
    if (!StatusIoctlSource::compiledWithAlsaHeaders()) {
        st_.state = ProducerState::kNoSource;
        st_.reason = "binario sin sound/asound.h: no hay fuente de timestamps";
        return false;
    }
    return true;
}

ProducerStatus HalClockProducer::poll() noexcept {
    st_.reason = "";
    if (!enabled_) { st_.state = ProducerState::kDisabled; st_.reason = "no habilitado"; return st_; }

    if (!ensureSource()) return st_;

    if (device_.card < 0) {
        const auto devices = discoverPlaybackDevices(cfg_.procRoot, cfg_.devRoot);
        if (devices.empty()) {
            st_.state = ProducerState::kNoDevices;
            st_.reason = "sin PCM de reproduccion en /proc/asound";
            return st_;
        }
        // Se prefiere el que tenga rate nominal declarado.
        const PcmDevice* pick = nullptr;
        for (const auto& d : devices) if (d.nominalRateHz > 0) { pick = &d; break; }
        if (!pick) pick = &devices.front();
        device_ = *pick;
        st_.device = device_.devPath;
        opened_ = src_->open(device_);
        if (!opened_) {
            st_.state = ProducerState::kNoSource;
            st_.reason = "no se pudo abrir el dispositivo PCM";
            return st_;
        }
    }

    PairedSample s{};
    if (!src_->read(s)) {
        st_.state = ProducerState::kWaitingSamples;
        st_.reason = "lectura sin muestra (ioctl fallo o sin cabeceras)";
        return st_;
    }

    const GateVerdict v = gate_.evaluate(s);
    if (!v.accepted) {
        if (gate_.streak() == 0) ++st_.rejected;
        st_.state = ProducerState::kRejected;
        st_.reason = v.reason;
        return st_;
    }

    if (!gate_.readyToPublish()) {
        st_.state = ProducerState::kWaitingSamples;
        st_.reason = "acumulando pares coherentes";
        return st_;
    }

    if (!publisher_) {
        auto* p = new ivanna::audio::HardwareClockPublisher();
        const char* path = cfg_.snapshotPath.empty()
                               ? ivanna::audio::kHardwareClockSnapshotPath
                               : cfg_.snapshotPath.c_str();
        if (!p->open(path)) {
            delete p;
            st_.state = ProducerState::kPublishFailed;
            st_.reason = "no se pudo abrir el snapshot de reloj";
            return st_;
        }
        publisher_ = p;
        publisherOpen_ = true;
    }

    auto* pub = static_cast<ivanna::audio::HardwareClockPublisher*>(publisher_);
    ivanna::audio::HardwareClockSample sample{};
    sample.presentedFrames = s.presentedFrames;
    sample.hardwareTimestampNs = s.timestampNs;
    sample.nominalSampleRateHz = s.nominalRateHz;
    if (!pub->publish(sample)) {
        st_.state = ProducerState::kPublishFailed;
        st_.reason = "publish() rechazo la muestra";
        return st_;
    }

    const double nominal = static_cast<double>(s.nominalRateHz);
    st_.state = ProducerState::kPublishing;
    st_.reason = v.reason;
    ++st_.published;
    st_.measuredRateHz = v.impliedRateHz;
    st_.driftPpm = nominal > 0.0 ? (v.impliedRateHz - nominal) / nominal * 1e6 : 0.0;
    st_.lastTimestampNs = s.timestampNs;
    return st_;
}

}  // namespace ivanna::audio::halclock
