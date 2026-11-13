#ifndef IVANNA_ASRC_IPC_H
#define IVANNA_ASRC_IPC_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace ivanna {
namespace asrc {

constexpr uint32_t kShmMagic = 0x41535243u;  // 'ASRC'
constexpr uint32_t kShmVersion = 1u;
constexpr size_t kShmSize = 64;

constexpr uint32_t kFlagKillSwitch = 1u << 0;
constexpr uint32_t kFlagDaemonAlive = 1u << 1;
constexpr uint32_t kFlagClipping = 1u << 2;

struct Block {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t seq;
    uint32_t crc;
    double ratio;
    double ppm;
    int64_t frames;
    int64_t ts_ns;
    uint32_t updates;
    uint32_t reserved;
    uint32_t seq_copy;
    uint32_t pad;
};

static_assert(sizeof(Block) == kShmSize, "Block debe medir 64 bytes");

uint32_t crcOf(const Block& b);
int64_t monotonicNs();
bool killSwitchPresent(const std::string& path);

class Writer {
public:
    Writer() = default;
    ~Writer();
    bool open(const std::string& path);
    void close();
    bool write(double ratio, double ppm, uint32_t flags,
               int64_t frames, int64_t ts_ns);
    const std::string& path() const { return path_; }
private:
    int fd_ = -1;
    std::string path_;
    uint32_t seq_ = 0;
    uint32_t updates_ = 0;
    bool writeRaw(const Block& b);
};

struct ReadResult {
    double ratio = 1.0;
    double ppm = 0.0;
    bool fresh = false;
    bool kill_switch = false;
    bool daemon_alive = false;
    uint32_t attempts = 0;
    const char* reason = "sin_dato";
};

class Reader {
public:
    Reader() = default;
    void open(const std::string& path, int max_age_ms = 200);
    void close();
    ReadResult read();
    // Lectura cruda validada (magic + seq par + crc). Se usa para la telemetría
    // de frames del efecto, que viaja en el mismo tipo de bloque.
    bool readBlock(Block* out) const;
    const std::string& path() const { return path_; }
private:
    int fd_ = -1;
    std::string path_;
    int max_age_ms_ = 200;
    bool readRaw(Block* out) const;
};

constexpr const char* kDefaultKillSwitch = "/data/adb/ivanna_omega/pal_asrc.off";
constexpr const char* kDefaultShm = "/data/adb/ivanna_omega/asrc_ratio.shm";

}  // namespace asrc
}  // namespace ivanna

#endif
