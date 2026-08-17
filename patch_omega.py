import re

with open("app/src/main/cpp/omega_effect.cpp", "r") as f:
    content = f.read()

# FASE 3: Integrate classifier
integration_code = """        // Render binaural de objetos (VBAP + HRTF) + DSP de salida
        fc->processStereo(L, R, (size_t)chunk);

        // FASE 3: Integración de TinyML Asíncrono
        if (auto* classifier = fc->getClassifier()) {
            uint8_t domClass = classifier->getDominantClass();
            // 0: Speech, 1: Music, 2: Transient, 3: Noise
            if (domClass == 0) {
                // Speech: Focus vocal (reducción espacial sutil)
                for (int n = 0; n < chunk; ++n) {
                    float m = (L[n] + R[n]) * 0.5f;
                    float s = (L[n] - R[n]) * 0.5f;
                    s *= 0.8f; // Atenuar side
                    L[n] = m + s;
                    R[n] = m - s;
                }
            } else if (domClass == 1) {
                // Music: Expansión estéreo armónica
                for (int n = 0; n < chunk; ++n) {
                    float m = (L[n] + R[n]) * 0.5f;
                    float s = (L[n] - R[n]) * 0.5f;
                    s *= 1.2f; // Expandir side
                    L[n] = m + s;
                    R[n] = m - s;
                }
            }
        }
"""

content = content.replace("        // Render binaural de objetos (VBAP + HRTF) + DSP de salida\n        fc->processStereo(L, R, (size_t)chunk);", integration_code)

with open("app/src/main/cpp/omega_effect.cpp", "w") as f:
    f.write(content)
