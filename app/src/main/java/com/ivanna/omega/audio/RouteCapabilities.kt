package com.ivanna.omega.audio

/**
 * Qué funciones NO llegan al audio según el modo de backend real (AudioBackendSelector.Mode).
 *
 * Fuente: docs/ROOT_VS_NOROOT_CAPABILITIES.md (auditoría por lectura de código; no medido en dispositivo).
 * Lista = acciones que OmegaEngineBridge envía solo al daemon Magisk, sin equivalente local verificado.
 * Se muestra al usuario para que un control sin efecto no parezca funcionar.
 */
object RouteCapabilities {

    /** Funciones que dependen del daemon (Ruta B) y no tienen espejo verificado en el motor de la app. */
    val daemonOnlyFeatures: List<String> = listOf(
        "Intensidad global del motor",
        "Estado perceptual enviado al efecto del sistema",
        "Métricas de pabellón auricular (pinna)",
        "Perfil por ruta de salida",
        "Estado adaptativo y puntuaciones YAMNet en el efecto del sistema",
        "WFS, crosstalk, upmixing, 4D y ejes supremos aplicados al audio del sistema",
        "Procesado del audio de todas las apps (sin root solo se procesa lo que la captura permite)"
    )

    /** Texto para la UI; null = no hay nada que advertir en este modo. */
    fun limitsFor(mode: AudioBackendSelector.Mode): Pair<String, List<String>>? = when (mode) {
        AudioBackendSelector.Mode.NO_ROOT ->
            "Modo sin root" to daemonOnlyFeatures
        AudioBackendSelector.Mode.ROOT_NO_DAEMON ->
            "Root sin daemon: el módulo Magisk no responde" to daemonOnlyFeatures
        AudioBackendSelector.Mode.ROOT_DAEMON, AudioBackendSelector.Mode.UNKNOWN -> null
    }
}
