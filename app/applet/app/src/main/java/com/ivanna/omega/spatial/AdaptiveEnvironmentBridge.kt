package com.ivanna.omega.spatial

import android.util.Log
/**
 * AdaptiveEnvironmentBridge
 *
 * Coordina la comunicación del RT60 acústico medido por RoomSimulator
 * hacia el AdaptiveDecisionEngine nativo. Permite que el factor de
 * adaptación λ_t se calcule con información real del entorno.
 *
 * AUDIT FIX PR-8: Conector entre RoomSimulator → AdaptiveDecisionEngine (RT60)
 */
object AdaptiveEnvironmentBridge {

    private const val TAG = "AdaptiveEnvironmentBridge"
    
    @Volatile
    private var lastRT60 = 0.3f  // Default initial value

    /**
     * Actualizar el RT60 acústico del entorno en el AdaptiveDecisionEngine nativo.
     * Típicamente llamado por RoomSimulator cada vez que recalcula.
     *
     * @param rt60 RT60 acústico medido (segundos)
     */
    fun updateEnvironmentRT60(rt60: Float) {
        val clampedRT60 = rt60.coerceIn(0.1f, 3.0f)
        if (Math.abs(clampedRT60 - lastRT60) < 0.01f) {
            return  // Evitar actualizaciones innecesarias
        }
        lastRT60 = clampedRT60

        if (com.ivanna.omega.core.IvannaNativeLib.isLoaded) {
            runCatching {
                com.ivanna.omega.core.IvannaNativeLib.nativeSetAdaptiveEnvironmentRT60(clampedRT60)
            }
        }
        Log.d(TAG, "updateEnvironmentRT60: $clampedRT60 s")
    }

    /**
     * Obtener el último RT60 seteado.
     * Para debugging o verificación.
     *
     * @return RT60 actual en segundos
     */
    fun getCurrentRT60(): Float = lastRT60
}
