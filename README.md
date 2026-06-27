# IVANNA-OMEGA-SUPREME
**© 2025–2026 Luis Uriel Pimentel Pérez · GORE TNS · LUPP-OR9**

Motor de audio neuromorfico unificado para Android. Software propietario y confidencial.

## Arquitectura

```
[AudioTrack / MediaPlayer]
        │
        ▼
  libomega_effect.so   ← AudioFlinger GlobalEffect (Magisk)
        │
        ▼
  libivanna_omega.so   ← NDK C++ DSP engine (APK)
        │
  ┌─────┴──────────────────────────┐
  │  GainStage (input)             │
  │  HarmonicExciter (Padé tanh)   │
  │  Compressor (peak feed-fwd)    │
  │  ParametricEQ (4-band biquad)  │
  │  StereoWidener (M/S)           │
  │  GainStage (output)            │
  │  ── mode 1: PI-LSTM Milenio ── │
  │    CT-LSTM RK4 @ 384kHz        │
  │    4x Polyphase Up/Down        │
  │    HRTF + 8 early reflections  │
  │  ── mode 2: + Ω-Atlas Spatial ─│
  │    Triadic equilibrium engine  │
  │    room_model + HRTF ITD       │
  └────────────────────────────────┘
        │
   SpectralClassifier  (FFT-1024 Kotlin, BPM, 4-class)
   EvolutionaryKernel  (GA 128 genomes, elitismo 4)
   PhaseOracle         (predicción de fase)
   NeuroCochlearManifold (32 canales Gammatone + Volterra)
   LIFNeuronPool32/128  (Leaky Integrate-and-Fire)
```

## Repos fusionados

| Repo | Contribución |
|---|---|
| IVANNA-FUSION-PRO | DSP core: ParametricEQ, Compressor, HarmonicExciter, StereoWidener, GainStage — todos los FIX |
| IVANNA-FUSION | SpectralClassifier, EvolutionaryKernel, PhaseOracle, SpatialEngine, MagiskBridge, DSPController, CI |
| IVANNA-ULTRA | PI-LSTM Milenio, NeuroCochlearManifold, VolterraH2, LIFNeuronPool, HexagonDSP, AudioForegroundService |
| IVANNURI-OMEGA-EQ-PLUS | NvsNP solver, Jiles-Atherton, Koren-Cordell tube model |
| IVANNA--FUSION | Magisk module skeleton, service.sh, sepolicy |
| IVANNURI_T-N-S | AiCalibrator stub |

## Build

```bash
./gradlew assembleDebug          # APK debug
./gradlew assembleRelease        # APK release
# CI produces Magisk zip automatically
```

## Modos de procesamiento

| Modo | Cadena |
|---|---|
| 0 — DSP | GainStage → Exciter → Comp → EQ → Widener |
| 1 — DSP+LSTM | + PI-LSTM Milenio (CT-LSTM RK4 + HRTF @ 384kHz) |
| 2 — FULL | + Ω-Atlas Spatial (triadic equilibrium) |
