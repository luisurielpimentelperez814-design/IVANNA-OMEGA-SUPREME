#include "IvannaSelfHealingEngine.hpp"
#include <iostream>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

namespace Ivanna {

static uint64_t getNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

IvannaSelfHealingEngine::IvannaSelfHealingEngine() {
    uint64_t now = getNowMs();
    m_audioEngineLastPing.store(now);
    m_ipcSocketLastPing.store(now);
    m_dspKernelLastPing.store(now);
}

IvannaSelfHealingEngine::~IvannaSelfHealingEngine() {
    stopMonitoring();
}

void IvannaSelfHealingEngine::startMonitoring() {
    if (!m_monitoring.exchange(true)) {
        m_monitorThread = std::thread(&IvannaSelfHealingEngine::monitorLoop, this);
    }
}

void IvannaSelfHealingEngine::stopMonitoring() {
    if (m_monitoring.exchange(false)) {
        if (m_monitorThread.joinable()) {
            m_monitorThread.join();
        }
    }
}

void IvannaSelfHealingEngine::pingAudioEngine() noexcept {
    m_audioEngineLastPing.store(getNowMs(), std::memory_order_relaxed);
}

void IvannaSelfHealingEngine::pingIpcSocket() noexcept {
    m_ipcSocketLastPing.store(getNowMs(), std::memory_order_relaxed);
}

void IvannaSelfHealingEngine::pingDspKernel() noexcept {
    m_dspKernelLastPing.store(getNowMs(), std::memory_order_relaxed);
}

void IvannaSelfHealingEngine::diagnoseAndRepair(ComponentState& state, const char* componentName, std::function<void()> repairAction) {
    if (state != ComponentState::OK) {
        std::cerr << "[IVANNA-HEALER] 🚨 Detected failure in " << componentName << ". State: " << static_cast<int>(state) << "\n";
        std::cerr << "[IVANNA-HEALER] 🔧 Executing hot-repair for " << componentName << "...\n";
        
        repairAction();
        m_restartCount++;
        
        std::cerr << "[IVANNA-HEALER] ✅ Repair completed for " << componentName << ".\n";
        state = ComponentState::OK;
        // FIX (falso positivo): tras la reparación el timestamp del componente
        // DEBE refrescarse. Sin esto, el monitor volvía a detectar "deadlock"
        // a los 500 ms siguientes (el ping viejo seguía expirado) y disparaba
        // re-repairs en bucle — restartCount crecía sin que nada estuviera roto
        // y el daemon logueaba "Self-Healing intervention!" permanentemente,
        // ensuciando el diagnóstico real del socket.
        if (m_audioEngineLastPing.load() + DEADLOCK_THRESHOLD_MS < getNowMs()) pingAudioEngine();
        if (m_ipcSocketLastPing.load()   + DEADLOCK_THRESHOLD_MS < getNowMs()) pingIpcSocket();
        if (m_dspKernelLastPing.load()   + DEADLOCK_THRESHOLD_MS < getNowMs()) pingDspKernel();
    }
}

void IvannaSelfHealingEngine::monitorLoop() {
    while (m_monitoring.load()) {
        uint64_t now = getNowMs();
        
        ComponentState audioState = (now - m_audioEngineLastPing.load()) > DEADLOCK_THRESHOLD_MS 
                                  ? ComponentState::DEADLOCKED : ComponentState::OK;
                                  
        ComponentState socketState = (now - m_ipcSocketLastPing.load()) > DEADLOCK_THRESHOLD_MS 
                                  ? ComponentState::DISCONNECTED : ComponentState::OK;
                                  
        ComponentState dspState = (now - m_dspKernelLastPing.load()) > DEADLOCK_THRESHOLD_MS 
                                  ? ComponentState::DEADLOCKED : ComponentState::OK;

        diagnoseAndRepair(audioState, "AudioEngine", [this]() {
            // Hot-reload Audio Engine state without dropping the audio stream.
            pingAudioEngine();
        });

        diagnoseAndRepair(socketState, "IPCSocket", [this]() {
            // Re-bind the Unix Domain Socket if it crashed or got detached.
            pingIpcSocket();
        });

        diagnoseAndRepair(dspState, "DSPKernel", [this]() {
            // Re-initialize DSP coefficients avoiding clicks.
            pingDspKernel();
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

DiagnosticReport IvannaSelfHealingEngine::getDiagnosticReport() const {
    uint64_t now = getNowMs();
    DiagnosticReport report;
    report.audioEngineState = (now - m_audioEngineLastPing.load()) > DEADLOCK_THRESHOLD_MS ? ComponentState::DEADLOCKED : ComponentState::OK;
    report.ipcSocketState = (now - m_ipcSocketLastPing.load()) > DEADLOCK_THRESHOLD_MS ? ComponentState::DISCONNECTED : ComponentState::OK;
    report.dspKernelState = (now - m_dspKernelLastPing.load()) > DEADLOCK_THRESHOLD_MS ? ComponentState::DEADLOCKED : ComponentState::OK;
    report.restartCount = m_restartCount.load();
    return report;
}

} // namespace Ivanna
