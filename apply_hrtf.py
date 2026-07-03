# -*- coding: utf-8 -*-
import io, os, shutil

def rep(path, old, new, label):
    with io.open(path, "r", encoding="utf-8") as f:
        s = f.read()
    if old not in s:
        print("SKIP (no match): " + label)
        return False
    s = s.replace(old, new, 1)
    with io.open(path, "w", encoding="utf-8") as f:
        f.write(s)
    print("OK: " + label)
    return True

PD = "app/src/main/cpp/pd_engine.hpp"
SPATIAL_DIR = "app/src/main/cpp/spatial"

# 0. Copiar los 3 headers nuevos (deben estar junto a este script)
for fn in ("fft_radix2.hpp", "synthetic_hrtf.hpp", "hrtf_convolver.hpp"):
    dst = os.path.join(SPATIAL_DIR, fn)
    if os.path.exists(dst):
        print("SKIP (ya existe): " + dst)
        continue
    if os.path.exists(fn):
        shutil.copy(fn, dst)
        print("OK: creado " + dst)
    else:
        print("FALTA: coloca " + fn + " junto a este script")

# 1. Include
rep(PD,
'''#include "neuromorphic/nho_engine.hpp"
#include "neuromorphic/biquad_envelope_bank.hpp"
#include "spatial/cue_based_spatial.hpp"
#include "control_frame.hpp"''',
'''#include "neuromorphic/nho_engine.hpp"
#include "neuromorphic/biquad_envelope_bank.hpp"
#include "spatial/cue_based_spatial.hpp"
#include "spatial/hrtf_convolver.hpp"
#include "control_frame.hpp"''',
"include hrtf_convolver.hpp")

# 2. Miembro nuevo + buffers reusables
rep(PD,
'''    NHOEngine           nho;
    BiquadEnvelopeBank  cue_bank;   // incluye refinamiento PhaseOracle (T_refined)
    CueBasedSpatial     spatial;''',
'''    NHOEngine           nho;
    BiquadEnvelopeBank  cue_bank;   // incluye refinamiento PhaseOracle (T_refined)
    CueBasedSpatial     spatial;
    // Modo 3: convolución binaural HRTF real (FFT overlap-save + crossfade).
    // Opera sobre el PCM ya presente en el pipeline (post EQ/Comp/Exciter/
    // Widener) — no decodifica ni intercepta ningún contenedor propietario.
    HRTFConvolver       hrtf_conv;''',
"miembro HRTFConvolver")

# 3. init()/reset()
rep(PD,
'''    void init(uint32_t sr) noexcept {
        sample_rate = (float)sr;
        cue_bank.init(sr);
        reset();
    }

    void reset() noexcept {
        for (int i = 0; i < PD_DIM; ++i) z[i] = z_prev[i] = 0.f;
        nho.reset();
        cue_bank.reset();
        spatial.reset();
    }''',
'''    void init(uint32_t sr) noexcept {
        sample_rate = (float)sr;
        cue_bank.init(sr);
        hrtf_conv.init(sr);
        reset();
    }

    void reset() noexcept {
        for (int i = 0; i < PD_DIM; ++i) z[i] = z_prev[i] = 0.f;
        nho.reset();
        cue_bank.reset();
        spatial.reset();
        hrtf_conv.reset();
    }''',
"init/reset: hrtf_conv")

# 4. mode clamp 0..2 -> 0..3
rep(PD,
'''    void set_mode_(int m)            noexcept { mode.store(std::clamp(m, 0, 2), std::memory_order_relaxed); }''',
'''    void set_mode_(int m)            noexcept { mode.store(std::clamp(m, 0, 3), std::memory_order_relaxed); }''',
"mode clamp 0..2 -> 0..3 (agrega modo 3 = HRTF binaural)")

# 5. applyControlFrame: cuando mode==3, alimenta hrtf_conv con los MISMOS
#    campos existentes (spatial_angle_deg, spatial_width) — sin controles
#    nuevos. spatial_width [0,2] se remapea a "agresividad" [0,1].
rep(PD,
'''    void applyControlFrame(const ControlFrame& f) noexcept {
        set_mode_(f.mode);
        set_nho_alpha_(f.nho_alpha);
        set_nho_beta_(f.nho_beta);
        set_nho_wet_(f.nho_wet);
        set_nho_harmonic_(f.nho_harmonic_gain);
        set_spatial_angle_(f.spatial_angle_deg);
        set_spatial_width_(f.spatial_width);
    }''',
'''    void applyControlFrame(const ControlFrame& f) noexcept {
        set_mode_(f.mode);
        set_nho_alpha_(f.nho_alpha);
        set_nho_beta_(f.nho_beta);
        set_nho_wet_(f.nho_wet);
        set_nho_harmonic_(f.nho_harmonic_gain);
        set_spatial_angle_(f.spatial_angle_deg);
        set_spatial_width_(f.spatial_width);
        // Modo 3 (HRTF binaural real): reutiliza los MISMOS 2 controles
        // espaciales que ya existían para CueBasedSpatial — cumple la
        // regla de los 13 controles, ningún control nuevo en la UI.
        // spatial_width [0,2] -> "agresividad HRTF" [0,1].
        const float aggressiveness = std::clamp(f.spatial_width * 0.5f, 0.f, 1.f);
        hrtf_conv.set_position(f.spatial_angle_deg, aggressiveness);
    }''',
"applyControlFrame: alimenta hrtf_conv sin controles nuevos")

# 6. process_block: rama mode==3
rep(PD,
'''        // Extract perceptual cues from block
        const PerceptualCues cues = cue_bank.process_block(inL, inR, n);

        // Alimenta al EvolutionaryKernel con cues reales — antes el GA optimizaba
        // contra una función fija sin relación con lo que sonaba (fitness audio-
        // agnóstico). Ahora busca genomas coherentes con el audio en curso.
        evo_update_audio_cues(cues.L, cues.T, cues.S);

        for (int i = 0; i < n; ++i) {
            float xL = inL[i], xR = inR[i];

            // Mode 1+: NHO harmonic shaping
            float nhL, nhR;
            nho.process_sample(xL, xR, nhL, nhR);

            if (m >= 2) {
                // Mode 2: spatial processing
                float sL, sR;
                spatial.process_sample(nhL, nhR, sL, sR, sample_rate);

                // State update with cues
                update_state(cues);

                // Decode: y = 0.6·x + 0.2·S + 0.2·tanh(mean(z))
                decode(xL, xR, sL, sR, outL[i], outR[i]);
            } else {
                // Mode 1: NHO only, no spatial
                update_state(cues);
                decode(xL, xR, nhL, nhR, outL[i], outR[i]);
            }
        }
    }''',
'''        // Extract perceptual cues from block
        const PerceptualCues cues = cue_bank.process_block(inL, inR, n);

        // Alimenta al EvolutionaryKernel con cues reales — antes el GA optimizaba
        // contra una función fija sin relación con lo que sonaba (fitness audio-
        // agnóstico). Ahora busca genomas coherentes con el audio en curso.
        evo_update_audio_cues(cues.L, cues.T, cues.S);

        if (m == 3) {
            // Modo 3: HRTF binaural real. El convolver opera en bloque
            // completo (internamente sub-divide en BLOCK=256), no por
            // muestra — más eficiente y evita romper la ventana FFT.
            hrtfNhL_.resize(n); hrtfNhR_.resize(n);
            hrtfSL_.resize(n);  hrtfSR_.resize(n);
            for (int i = 0; i < n; ++i)
                nho.process_sample(inL[i], inR[i], hrtfNhL_[i], hrtfNhR_[i]);

            hrtf_conv.process(hrtfNhL_.data(), hrtfNhR_.data(),
                               hrtfSL_.data(), hrtfSR_.data(), n);

            for (int i = 0; i < n; ++i) {
                update_state(cues);
                decode(inL[i], inR[i], hrtfSL_[i], hrtfSR_[i], outL[i], outR[i]);
            }
            return;
        }

        for (int i = 0; i < n; ++i) {
            float xL = inL[i], xR = inR[i];

            // Mode 1+: NHO harmonic shaping
            float nhL, nhR;
            nho.process_sample(xL, xR, nhL, nhR);

            if (m >= 2) {
                // Mode 2: spatial processing
                float sL, sR;
                spatial.process_sample(nhL, nhR, sL, sR, sample_rate);

                // State update with cues
                update_state(cues);

                // Decode: y = 0.6·x + 0.2·S + 0.2·tanh(mean(z))
                decode(xL, xR, sL, sR, outL[i], outR[i]);
            } else {
                // Mode 1: NHO only, no spatial
                update_state(cues);
                decode(xL, xR, nhL, nhR, outL[i], outR[i]);
            }
        }
    }''',
"process_block: rama modo 3 (HRTF binaural)")

# 7. Buffers reusables privados (evita allocs nuevos cada bloque salvo resize)
rep(PD,
'''    std::thread        evo_thread_;
    std::atomic<bool>  evo_running_{false};''',
'''    // Buffers reusables para el modo 3 (se reencogen solo si n crece)
    std::vector<float> hrtfNhL_, hrtfNhR_, hrtfSL_, hrtfSR_;

    std::thread        evo_thread_;
    std::atomic<bool>  evo_running_{false};''',
"buffers reusables modo 3")

print("DONE")
