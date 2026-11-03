# IVANNA OMEGA SUPREME — Hardware Validation Prerelease

- Version: v2.5.236
- Date: 2026-11-02
- Base integration: PR #20, merged to main
- Target device: Moto G85 5G (rooted, Magisk)

## Included repairs

- Adaptive Decision Engine startup is gated on successful native DSP graph initialization.
- Android/Linux thread lifecycle uses joinable pthreads so resource errors remain recoverable with -fno-exceptions.
- start/stop/join is serialized; concurrent lifecycle regression coverage is included.
- User adaptive preference and manual mode remain authoritative across startup and daemon reconnect.
- Manual compressor amount is preserved when adaptive processing is disabled; ADE protection is arbitrated separately.
- Audio metric consumers honor PCM/voice/spectral validity and reject synthetic bootstrap metrics.
- Limiter latency benchmark now uses sorted samples and a monotonic clock.

## CI evidence

- Native route, ASRC, adaptive metric and actuator contracts: passed.
- Release CTest: passed.
- Host CTest with ASan/UBSan: passed.
- Android APK and ARM64 daemon/client build: passed.
- Magisk module packaging, effect-library export validation and staging bundle verification: passed.

## Device gate — NOT YET CERTIFIED

Do not treat this prerelease as proof that AudioFlinger loaded libomega_effect.so or that HRTF/ASRC processed real PCM on the Moto G85. Install the module through Magisk so customize.sh merges the OEM audio_effects.xml, then capture runtime effect-chain, audioserver library-map and PCM-application evidence before production promotion.