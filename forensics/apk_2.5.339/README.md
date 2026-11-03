# APK source recovery — IVANNA OMEGA v2.5.339 (630)

This directory preserves the full Kotlin source files from the repository release tag `v2.5.339`, which matches the version embedded in the supplied APK.

## Provenance and verification

- APK SHA-256: `7dc78d9b13a9fc47a3887a60ffc57ae89196fc00b9d0fe5a2835195a0659b1dc`
- APK DEX version string: `2.5.339`
- DEX also embeds the release URL for `v2.5.339`.
- The supplied APK contains DEX descriptors for `DSPBridge`, `AdaptiveDSPState`, `RealtimeLearningController`, and both `com.ivanna.omega.PerceptualDecisionEngine` and `com.ivanna.omega.ai.PerceptualDecisionEngine`.
- Source files were fetched from the matching Git tag, not reconstructed from guessed method signatures.

## Kotlin source snapshots

The `kotlin/` subtree contains complete source files, including method bodies, for:
- DSP bridge and state: `DSPBridge`, `DSPState`, `AdaptiveDSPState`
- Learning path: `RealtimeLearningController`, `LearningBias`, `AdaptiveLearning`, `AIInferenceEngine`, `ModelManager`
- Perceptual path: both `PerceptualDecisionEngine` classes, `PerceptualCortex`, `PerceptualBrainEngine`
- Native bridge and runtime: `IvannaNativeLib`, `IvannaControlLoop`, `IvannaBridgePlayer`
- Parameter transitions: `AudioParameterManager`

The package-root `PerceptualDecisionEngine.kt` is extracted verbatim from the corresponding top-level class block in `MainActivity.kt` at the same tag.

## Important terminology

These are release-tag source snapshots corresponding to the APK's declared version and DEX class descriptors. They are **not represented as a byte-for-byte JADX decompilation**. The APK itself was independently inspected for version strings, DEX descriptors, ELF architecture, SHA-256 and dynamic symbols. No Java/Kotlin decompiler was available in the execution environment. The snapshots preserve full original method bodies where the matching release source exists.

See [NATIVE_ELF_EVIDENCE.md](NATIVE_ELF_EVIDENCE.md) for the exact native-library identity and exported evolutionary/DSP symbols.
