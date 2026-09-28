#pragma once

#include <cstdint>
#include <atomic>
#include <mutex>
#include <string>

namespace Ivanna {

struct AgentMemorySnapshot {
    uint64_t timestampMs;
    uint8_t dominantAcousticClass;
    float avgProsodyPitch;
    float longTermThermalLoad;
    float userLoudnessPreference;
    uint32_t sessionReboots;
    
    // Hashes/Checksums for integrity
    uint32_t magic;
    uint32_t crc32;
};

/**
 * ════════════════════════════════════════════════════════════════════════
 * IVANNA SUPER AGENT MEMORY (Persistent State Engine)
 * ════════════════════════════════════════════════════════════════════════
 * Implements a memory-mapped lock-free persistent state for the daemon.
 * It stores acoustic/contextual long-term data (hours/days) to adapt
 * the DSP immediately upon boot, without requiring a cold-start learning phase.
 * ════════════════════════════════════════════════════════════════════════
 */
class IvannaSuperAgentMemory {
public:
    IvannaSuperAgentMemory();
    ~IvannaSuperAgentMemory();

    bool initialize(const char* mmapFilePath);
    
    // Updates internal knowledge graph (called from low-priority ML threads)
    void updateContext(uint8_t acousticClass, float pitch, float loudnessPref) noexcept;
    
    // Invoked to commit rolling memory to mapped disk
    void commitToDisk() noexcept;

    AgentMemorySnapshot getLatestSnapshot() const noexcept;

private:
    static constexpr uint32_t MEMORY_MAGIC = 0x5550524D; // "UPRM"
    
    int m_fd = -1;
    AgentMemorySnapshot* m_mappedRegion = nullptr;
    AgentMemorySnapshot m_localState{};
    
    std::mutex m_commitMutex;
    
    uint32_t calculateCRC(const AgentMemorySnapshot& snap) const noexcept;
};

} // namespace Ivanna
