// IvannaUnifiedNative.kt
// ============================================================================
// Kotlin JNI Wrapper for Unified Engine (6 Motors)
// ============================================================================
package com.ivanna.omega.unified

object IvannaUnifiedNative {
    init {
        System.loadLibrary("ivanna_unified")
    }
    
    // ========================================================================
    // ENGINE LIFECYCLE
    // ========================================================================
    external fun nativeInitialize(): Boolean
    external fun nativeShutdown(): Boolean
    external fun nativeGetStatus(): Int  // 0=IDLE, 1=INITIALIZING, 2=RUNNING, 3=ERROR
    
    // ========================================================================
    // MOTOR 1: YAMNET (Anti-Dolby Audio Classification)
    // ========================================================================
    external fun nativeEnableAntiDolby(enabled: Boolean): Boolean
    external fun nativeUpdateYAMNetScores(
        voice: Float,
        music: Float,
        bass: Float,
        silence: Float
    )
    external fun nativeGetYAMNetScore(scoreType: Int): Float  // 0=voice, 1=music, 2=bass, 3=silence
    
    // ========================================================================
    // MOTOR 2: AUDIO ENGINE (DSP Parameters)
    // ========================================================================
    external fun nativeSetDSPParam(paramName: String, value: Float): Boolean
    external fun nativeGetDSPParam(paramName: String): Float
    external fun nativeGetAllDSPParams(): FloatArray
    
    // DSP Parameter names:
    // "exciter_wet" → [0..1] harmonic excitation
    // "eq_gain" → ±18 dB
    // "widener" → [0..1] stereo width
    // "comp_ratio" → 1:1..20:1
    // "route_bass_boost" → dB boost for route compensation
    
    // ========================================================================
    // MOTOR 3: SPATIAL ENGINE (3D Audio / HRTF)
    // ========================================================================
    external fun nativeEnableSpatial(enabled: Boolean): Boolean
    external fun nativeSetSpatialAngle(angleDeg: Float)
    external fun nativeSetSpatialWidth(width: Float)
    external fun nativeGetSpatialAngle(): Float
    
    // ========================================================================
    // MOTOR 4: EVOLUTIONARY KERNEL (Genetic Algorithm Optimization)
    // ========================================================================
    external fun nativeEnableEvolutionary(enabled: Boolean): Boolean
    external fun nativeGetEvolutionaryGen(): Int
    external fun nativeGetEvolutionaryGenome(): FloatArray
    
    // Genome layout (12 floats):
    // [0..4]   → DSP parameters (EQ, Comp, Exciter, Widener, GainStage)
    // [5..8]   → NHO parameters (harmonic, freq, phase, gain)
    // [9..11]  → Spatial parameters (angle, width, intensity)
    
    // ========================================================================
    // MOTOR 5: PHASE ORACLE (Predictive Audio Analysis)
    // ========================================================================
    external fun nativeEnablePhaseOracle(enabled: Boolean): Boolean
    external fun nativeGetPhaseRefinement(): Float
    external fun nativeGetPhaseCoherence(): Float
    
    // ========================================================================
    // MOTOR 6: OMEGA BRIDGE (Magisk Daemon Communication)
    // ========================================================================
    external fun nativeReconnectOmegaDaemon(): Boolean
    external fun nativeIsOmegaConnected(): Boolean
    
    // ========================================================================
    // MONITORING & TELEMETRY
    // ========================================================================
    external fun nativeGetMotorHealth(): BooleanArray  // [yamnet, audio, spatial, evo, phase, omega]
    external fun nativeGetOutputLUFS(): Float
    external fun nativeGetOutputPeak(): Float
    
    // ========================================================================
    // ROUTING & PROFILES
    // ========================================================================
    external fun nativeSetRouteProfile(routeType: Int)
    // routeType: 0=Speaker, 1=Wired, 2=Bluetooth, 3=USB
}
