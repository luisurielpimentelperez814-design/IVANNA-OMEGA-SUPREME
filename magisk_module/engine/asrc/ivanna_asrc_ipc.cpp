// ivanna_asrc_ipc.cpp — seqlock sobre pwrite/pread. Sin mmap (auditoría C-1, C-2).
#include "ivanna_asrc_ipc.h"

#include <cerrno>
#include <cmath>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ivanna {
namespace asrc {

uint32_t crcOf(const Block& b) {
    Block t = b;
    t.crc = 0;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&t);
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < sizeof(Block); ++i) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

int64_t monotonicNs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

bool killSwitchPresent(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

Writer::~Writer() { close(); }

bool Writer::open(const std::string& path) {
    close();
    path_ = path;
    fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd_ < 0) return false;
    seq_ = 0;
    updates_ = 0;
    return write(1.0, 0.0, kFlagDaemonAlive, 0, monotonicNs());
}

void Writer::close() {
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

bool Writer::writeRaw(const Block& b) {
    if (fd_ < 0) return false;
    const ssize_t n = ::pwrite(fd_, &b, sizeof(Block), 0);
    return n == static_cast<ssize_t>(sizeof(Block));
}

bool Writer::write(double ratio, double ppm, uint32_t flags,
                   int64_t frames, int64_t ts_ns) {
    if (fd_ < 0) return false;
    if (!std::isfinite(ratio) || !std::isfinite(ppm)) return false;

    Block b{};
    b.magic = kShmMagic;
    b.version = static_cast<uint16_t>(kShmVersion);
    b.flags = static_cast<uint16_t>(flags);
    b.ratio = ratio;
    b.ppm = ppm;
    b.frames = frames;
    b.ts_ns = ts_ns;
    b.updates = ++updates_;

    b.seq = ++seq_;              // impar: escritura en curso
    b.seq_copy = b.seq;
    b.crc = crcOf(b);
    if (!writeRaw(b)) return false;

    b.seq = ++seq_;              // par: publicada
    b.seq_copy = b.seq;
    b.crc = crcOf(b);
    return writeRaw(b);
}

void Reader::open(const std::string& path, int max_age_ms) {
    close();
    path_ = path;
    max_age_ms_ = max_age_ms;
    fd_ = ::open(path.c_str(), O_RDONLY);
}

void Reader::close() {
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

bool Reader::readRaw(Block* out) const {
    if (fd_ < 0) return false;
    const ssize_t n = ::pread(fd_, out, sizeof(Block), 0);
    return n == static_cast<ssize_t>(sizeof(Block));
}

bool Reader::readBlock(Block* out) const {
    if (!out) return false;
    for (int i = 0; i < 3; ++i) {
        Block a{}, b{};
        if (!readRaw(&a)) return false;
        if (a.magic != kShmMagic || a.version != kShmVersion) return false;
        if (a.seq & 1u) continue;
        if (!readRaw(&b)) return false;
        if (a.seq != b.seq) continue;
        if (a.crc != crcOf(a)) return false;
        *out = a;
        return true;
    }
    return false;
}

ReadResult Reader::read() {
    ReadResult r;
    if (fd_ < 0) { r.reason = "sin_canal"; return r; }

    Block a{}, b{};
    bool consistent = false;
    for (uint32_t attempt = 0; attempt < 3; ++attempt) {
        r.attempts = attempt + 1;
        if (!readRaw(&a)) { r.reason = "lectura_corta"; return r; }
        if (a.magic != kShmMagic || a.version != kShmVersion) {
            r.reason = "magic_invalido";
            return r;
        }
        r.kill_switch = (a.flags & kFlagKillSwitch) != 0;
        r.daemon_alive = (a.flags & kFlagDaemonAlive) != 0;
        if (a.seq & 1u) continue;
        if (!readRaw(&b)) { r.reason = "lectura_corta"; return r; }
        if (a.seq != b.seq) continue;
        if (a.crc != crcOf(a)) { r.reason = "crc_invalido"; return r; }
        consistent = true;
        break;
    }
    if (!consistent) { r.reason = "seqlock_inestable"; return r; }
    if (r.kill_switch) { r.reason = "kill_switch"; return r; }

    if (a.ts_ns > 0 && max_age_ms_ > 0) {
        const int64_t age_ns = monotonicNs() - a.ts_ns;
        if (age_ns > static_cast<int64_t>(max_age_ms_) * 1000000LL) {
            r.reason = "bloque_viejo";
            return r;
        }
    }
    if (!std::isfinite(a.ratio)) { r.reason = "ratio_no_finito"; return r; }
    if (std::fabs((a.ratio - 1.0) * 1e6) > 500.0 + 1e-9) {
        r.reason = "ratio_fuera_de_riel";
        return r;
    }
    r.ratio = a.ratio;
    r.ppm = a.ppm;
    r.fresh = true;
    r.reason = "ok";
    return r;
}

}  // namespace asrc
}  // namespace ivanna
