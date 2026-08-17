#pragma once
// OmegaControlBus.h - Bus de control cross-process usando SeqlockBus
// Arquitectura revolucionaria para audio inmersivo de clase mundial

#include "OmegaDspSnapshot.h"
#include <atomic>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <android/log.h>

namespace omega {

#define OMEGA_SHM_NAME "/omega_control_bus"
#define OMEGA_SHM_SIZE 4096

class OmegaControlBus {
public:
    static OmegaControlBus& instance() {
        static OmegaControlBus bus;
        return bus;
    }
    
    bool initialize(bool writer = false) {
        m_fd = shm_open(OMEGA_SHM_NAME, O_RDWR | (writer ? O_CREAT : 0), 0666);
        if (m_fd < 0) {
            __android_log_print(ANDROID_LOG_ERROR, "OmegaBus", "shm_open failed: %s", strerror(errno));
            return false;
        }
        
        if (writer && ftruncate(m_fd, OMEGA_SHM_SIZE) < 0) {
            __android_log_print(ANDROID_LOG_ERROR, "OmegaBus", "ftruncate failed");
            close(m_fd);
            return false;
        }
        
        m_base = mmap(nullptr, OMEGA_SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0);
        if (m_base == MAP_FAILED) {
            __android_log_print(ANDROID_LOG_ERROR, "OmegaBus", "mmap failed");
            close(m_fd);
            return false;
        }
        
        m_snapshot = reinterpret_cast<OmegaDspSnapshot*>(m_base);
        m_is_writer = writer;
        m_ready = true;
        
        __android_log_print(ANDROID_LOG_INFO, "OmegaBus", 
            "Initialized as %s at %p", writer ? "WRITER" : "READER", m_base);
        return true;
    }
    
    bool publish(const OmegaDspSnapshot& snapshot) {
        if (!m_ready || !m_is_writer || !m_snapshot) return false;
        
        OmegaDspSnapshot copy = snapshot;
        copy.generation = m_generation.fetch_add(1) + 1;
        copy.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        copy.crc32 = copy.computeCRC32();
        
        // Seqlock write
        m_seqlock.fetch_add(1, std::memory_order_release);
        std::memcpy(m_snapshot, &copy, sizeof(OmegaDspSnapshot));
        m_seqlock.fetch_add(1, std::memory_order_release);
        
        return true;
    }
    
    bool tryAcquireLatest(OmegaDspSnapshot& out) {
        if (!m_ready || !m_snapshot) return false;
        
        uint32_t seq1, seq2;
        do {
            seq1 = m_seqlock.load(std::memory_order_acquire);
            if (seq1 & 1) continue; // Write in progress
            
            std::memcpy(&out, m_snapshot, sizeof(OmegaDspSnapshot));
            
            seq2 = m_seqlock.load(std::memory_order_acquire);
        } while (seq1 != seq2);
        
        return out.isValid();
    }
    
    uint64_t lastPublishedGeneration() const {
        return m_generation.load(std::memory_order_relaxed);
    }
    
    bool isReady() const { return m_ready; }
    
    ~OmegaControlBus() {
        if (m_base && m_base != MAP_FAILED) {
            munmap(m_base, OMEGA_SHM_SIZE);
        }
        if (m_fd >= 0) {
            close(m_fd);
        }
    }

private:
    OmegaControlBus() : m_fd(-1), m_base(nullptr), m_snapshot(nullptr), 
                        m_ready(false), m_is_writer(false), m_generation(0) {}
    
    int m_fd;
    void* m_base;
    OmegaDspSnapshot* m_snapshot;
    std::atomic<bool> m_ready;
    bool m_is_writer;
    std::atomic<uint64_t> m_generation;
    std::atomic<uint32_t> m_seqlock{0};
};

} // namespace omega
