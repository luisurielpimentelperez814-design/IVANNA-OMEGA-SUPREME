# IVANNA UNIVERSAL IMMERSIVE RENDERER v9.0

## Universal Audio Object Engine & Binaural Spatializer

IVANNA OMEGA SUPREME v9.0 is an open-source, ultra-low latency (<5ms) universal object-based spatial audio rendering engine for Android (Magisk/Root & Non-Root).

### Key Features
- **Universal Object Extraction**: Bitstream parsing for Dolby Atmos (JOC/OAM), Sony 360RA (MPEG-H 3D), DTS:X Ultra, and ADM BWF.
- **Fallback Perceptual Extractor**: Real-time Blind Source Separation (BSS) and Interaural Cross-Correlation (IACC) for stereo-to-3D upmixing.
- **VBAP & Bilinear HRTF Interpolation**: ARM64 NEON vectorized 128-tap FIR convolution engine.
- **Image-Source Room Simulator**: Customizable acoustic reflections and early/late reverberation tail.
- **Kernel-Level TinyML Classifier**: YAMNet replacement powered by 64-Band Mel-STFT and Depthwise ConvNeXt.
- **SCHED_FIFO RT Daemon**: Zero-malloc, lock-free C++ audio loop running at `/dev/socket/ivanna_omega`.

### Architecture
INPUT AUDIO STREAM
├── Dolby Atmos / Sony 360RA / DTS:X / MPEG-H / Stereo
▼
OBJECT EXTRACTION & PARSING
├── Metadata Abstraction (AudioObject / AudioScene)
▼
HYBRID RENDERER (C++ ARM64 NEON)
├── Vector Base Amplitude Panning (VBAP)
├── Bilinear HRTF Convolution (CIPIC / KEMAR)
├── Room Acoustics Simulation
▼
BINAURAL STEREO OUTPUT (< 5 ms Latency)
code
Code
