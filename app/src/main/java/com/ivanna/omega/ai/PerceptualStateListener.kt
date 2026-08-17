package com.ivanna.omega.ai

/**
 * PerceptualStateListener
 *
 * Interfaz para que componentes se suscriban a cambios de PerceptualState
 * calculados por PerceptualCortex. Permite desacoplamiento: PerceptualCortex
 * no necesita conocer quién consuma su salida.
 *
 * Implementadores: IvannaBridgePlayer, DSPBridge, PlaybackCaptureService, etc.
 */
interface PerceptualStateListener {
    /**
     * Llamado cada vez que PerceptualCortex calcula un nuevo PerceptualState.
     * Los implementadores deben aplicar dinámicamente los parámetros al pipeline.
     *
     * @param state Nuevo estado perceptual calculado
     * @param deltaMs tiempo transcurrido desde la última llamada (ms)
     */
    fun onPerceptualStateChanged(state: PerceptualState, deltaMs: Long = 0L)
}
