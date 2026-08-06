package com.ivanna.omega.saf

/**
 * SaFBridge — JNI interface to SaFOptimizer (Φ_SAF^∞).
 * Library loaded by IvannaNativeLib; no separate load needed.
 */
object SaFBridge {
    @JvmStatic external fun nativeSaFInit(jsonPath: String): Boolean
    @JvmStatic external fun nativeSaFFeedback(direction: Int, correct: Boolean)
    @JvmStatic external fun nativeSaFGetParams(): FloatArray?
    @JvmStatic external fun nativeSaFGetIteration(): Int
    @JvmStatic external fun nativeSaFReset()
    @JvmStatic external fun nativeSaFIsConverged(): Boolean
    @JvmStatic external fun nativeSaFGetError(): Float
}
