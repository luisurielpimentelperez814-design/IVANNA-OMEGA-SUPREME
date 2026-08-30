#include "IvannaFusionCore.hpp"
#include "IvannaTinyML.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <csignal>

using namespace Ivanna;

static std::atomic<bool> g_running{true};

void signalHandler(int signum) {
    std::cout << "\n[IVANNA-DAEMON] Signal " << signum << " received. Terminating gracefully...\n";
    g_running.store(false);
}

int main() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::cout << "[IVANNA-DAEMON] Starting Anti-Dolby Audio Engine Daemon (Magisk SHM Mode)...\n";

    IvannaFusionEngine engine;
    engine.runAcousticProfiling();
    engine.setGoldenEarMode(true);

    alignas(16) AudioBuffer block = {0};

    uint64_t frameCounter = 0;
    auto lastReport = std::chrono::steady_clock::now();

    while (g_running.load()) {
        // Simulador de Ingesta desde Ring Buffer Inter-Proceso (SHM)
        engine.process(&block);
        frameCounter++;

        // Reporte periódico de clasificación TinyML cada ~1.0 segundo
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastReport).count() >= 1) {
            IvannaTinyML* classifier = engine.getClassifier();
            if (classifier) {
                const float* probs = classifier->getProbabilities();
                uint8_t domClass = classifier->getDominantClass();

                const char* classNames[] = {"Speech/Vocal", "Music/Spatial", "Transient/Impact", "Noise/Ambient"};

                std::cout << "[IVANNA-DAEMON] Block #" << frameCounter 
                          << " | Dominant Scene: " << classNames[domClass % 4]
                          << " [P_Vocal: " << probs[0] 
                          << ", P_Music: " << probs[1] 
                          << ", P_Impact: " << probs[2] 
                          << ", P_Noise: " << probs[3] << "]\n";
            }
            lastReport = now;
        }

        // Mantener cadencia de 48kHz (1024 samples = ~21.3ms per block)
        std::this_thread::sleep_for(std::chrono::microseconds(21333));
    }

    std::cout << "[IVANNA-DAEMON] Stopped cleanly.\n";
    return 0;
}
