#include "IvannaSuperAgentMemory.hpp"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <chrono>

namespace Ivanna {

static uint64_t currentMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

IvannaSuperAgentMemory::IvannaSuperAgentMemory() = default;

IvannaSuperAgentMemory::~IvannaSuperAgentMemory() {
    if (m_mappedRegion && m_mappedRegion != reinterpret_cast<void*>(-1)) {
        munmap(m_mappedRegion, sizeof(AgentMemorySnapshot));
    }
    if (m_fd >= 0) {
        close(m_fd);
    }
}

uint32_t IvannaSuperAgentMemory::calculateCRC(const AgentMemorySnapshot& snap) const noexcept {
    // Simple XOR folding for fast CRC
    const uint8_t* data = reinterpret_cast<const uint8_t*>(&snap);
    size_t len = offsetof(AgentMemorySnapshot, crc32);
    uint32_t crc = 0x811c9dc5;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        crc *= 0x01000193;
    }
    return crc;
}

bool IvannaSuperAgentMemory::initialize(const char* mmapFilePath) {
    m_fd = ::open(mmapFilePath, O_RDWR | O_CREAT, 0644);
    if (m_fd < 0) return false;

    struct stat st;
    if (::fstat(m_fd, &st) == 0 && st.st_size < static_cast<off_t>(sizeof(AgentMemorySnapshot))) {
        if (::ftruncate(m_fd, sizeof(AgentMemorySnapshot)) < 0) {
            close(m_fd);
            return false;
        }
    }

    void* addr = ::mmap(nullptr, sizeof(AgentMemorySnapshot), PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0);
    if (addr == MAP_FAILED) {
        close(m_fd);
        return false;
    }

    m_mappedRegion = reinterpret_cast<AgentMemorySnapshot*>(addr);

    if (m_mappedRegion->magic == MEMORY_MAGIC && m_mappedRegion->crc32 == calculateCRC(*m_mappedRegion)) {
        m_localState = *m_mappedRegion;
    } else {
        // Init fresh memory
        m_localState.timestampMs = currentMs();
        m_localState.dominantAcousticClass = 0;
        m_localState.avgProsodyPitch = 0.0f;
        m_localState.longTermThermalLoad = 0.0f;
        m_localState.userLoudnessPreference = -14.0f;
        m_localState.sessionReboots = 0;
        m_localState.magic = MEMORY_MAGIC;
        commitToDisk();
    }
    
    return true;
}

void IvannaSuperAgentMemory::updateContext(uint8_t acousticClass, float pitch, float loudnessPref) noexcept {
    // Exponential Moving Averages for long-term knowledge retention
    m_localState.dominantAcousticClass = acousticClass;
    
    if (pitch > 0.0f) {
        m_localState.avgProsodyPitch = (m_localState.avgProsodyPitch == 0.0f) 
                                       ? pitch 
                                       : m_localState.avgProsodyPitch * 0.99f + pitch * 0.01f;
    }
    
    m_localState.userLoudnessPreference = m_localState.userLoudnessPreference * 0.95f + loudnessPref * 0.05f;
    m_localState.timestampMs = currentMs();
}

void IvannaSuperAgentMemory::commitToDisk() noexcept {
    if (!m_mappedRegion) return;
    
    std::lock_guard<std::mutex> lock(m_commitMutex);
    m_localState.crc32 = calculateCRC(m_localState);
    std::memcpy(m_mappedRegion, &m_localState, sizeof(AgentMemorySnapshot));
    
    // Optional: msync(m_mappedRegion, sizeof(AgentMemorySnapshot), MS_ASYNC);
}

AgentMemorySnapshot IvannaSuperAgentMemory::getLatestSnapshot() const noexcept {
    return m_localState;
}

} // namespace Ivanna
