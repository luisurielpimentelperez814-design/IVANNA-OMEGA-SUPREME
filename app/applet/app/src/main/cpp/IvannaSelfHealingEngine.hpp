#pragma once

#include <atomic>
#include <thread>
#include <chrono>
#include <string>
#include <functional>
#include <vector>

namespace Ivanna {

enum class ComponentState {
    OK,
    WARNING,
    DEADLOCKED,
    DISCONNECTED,
    CRASHED
};

struct DiagnosticReport {
    ComponentState audioEngineState;
    ComponentState ipcSocketState;
    ComponentState dspKernelState;
    uint32_t restartCount;
    std::string lastError;
};

class IvannaSelfHealingEngine {
public:
    IvannaSelfHealingEngine();
    ~IvannaSelfHealingEngine();

    void startMonitoring();
    void stopMonitoring();

    // Heartbeat endpoints for components
    void pingAudioEngine() noexcept;
    void pingIpcSocket() noexcept;
    void pingDspKernel() noexcept;

    DiagnosticReport getDiagnosticReport() const;

private:
    void monitorLoop();
    void diagnoseAndRepair(ComponentState& state, const char* componentName, std::function<void()> repairAction);

    std::atomic<bool> m_monitoring{false};
    std::thread m_monitorThread;

    std::atomic<uint64_t> m_audioEngineLastPing{0};
    std::atomic<uint64_t> m_ipcSocketLastPing{0};
    std::atomic<uint64_t> m_dspKernelLastPing{0};

    std::atomic<uint32_t> m_restartCount{0};

    // Thresholds (ms)
    static constexpr uint64_t DEADLOCK_THRESHOLD_MS = 2000;
};

} // namespace Ivanna
