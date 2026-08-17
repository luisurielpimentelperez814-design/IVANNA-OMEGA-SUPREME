package com.ivanna.omega.audio

/**
 * Interruptor global para el procesador Volterra H2.
 * Se activa/desactiva desde la UI (IvannaControlPanel → MainActivity)
 * y se consulta desde el hilo de audio (IvannaBridgePlayer).
 */
object VolterraSwitch {
    @Volatile var enabled: Boolean = false
}
