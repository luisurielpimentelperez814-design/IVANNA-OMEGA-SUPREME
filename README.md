# IVANNA OMEGA SUPREME v6.0 — Cognitive Audio Engine

The world's first perceptual cognitive audio ecosystem for Android with Magisk kernel integration.

## Architecture & Data Flow
1. **PerceptualBrain (Kotlin)**: Process 12-dimensional psychoacoustic snapshot.
2. **DecisionEngine**: Combines analytical rules + TinyML MLP Q-learning.
3. **OmegaBridge**: High-speed Unix domain socket client (`/dev/socket/ivanna_omega`).
4. **MagiskDaemon (C++)**: Real-time daemon running at `SCHED_FIFO` priority.
5. **NativeDSP (C++)**: ARM NEON accelerated multiband compression, harmonic exciter, binaural HRTF, dynamic EQ.
