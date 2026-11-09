package com.ivanna.omega.audio

/**
 * Qué funciones NO llegan al audio según el modo de backend real (AudioBackendSelector.Mode).
 *
 * Fuente: docs/ROOT_VS_NOROOT_CAPABILITIES.md (auditoría por lectura de código; no medido en dispositivo).
 *
 * Auditoría v2.5.369 (2026-11-08): la cadena DSP completa YA corre en Ruta A
 * (capture) y la mayoría de los parámetros YA tienen espejo local verificado.
 * Por eso esta lista se redujo a lo que de verdad solo existe en el daemon.
 * Detalle por función en docs/ROOT_VS_NOROOT_CAPABILITIES.md ("Auditoría v2.5.369").
 *
 * Ya verificado en Ruta A (NO van en esta lista):
 * - Estado perceptual: sus parámetros se fijan por JNI local (restorer/UI).
 * - Pinna: el efecto real es el matching de sujeto HRTF en proceso (setHrtfSubject).
 * - Perfil por ruta de salida: AudioEngine.setRouteProfile → nativeSetRouteProfile.
 * - Estado adaptativo + YAMNet: motor adaptativo local + control_set_yamnet_scores.
 * - WFS, crosstalk, upmixing, 4D y ejes supremos: módulos en nativeProcessBlock,
 *   gobernados por flags locales que el restorer fija al arrancar.
 * - ORBE y HOLOS: activos por defecto en v2.5.369 (ORBE 1.0, HOLOS 0.5 / K=0).
 *
 * Se muestra al usuario para que un control sin efecto no parezca funcionar.
 */
object RouteCapabilities {

    /** Funciones que dependen del daemon (Ruta B) y no tienen espejo en el motor de la app. */
    val daemonOnlyFeatures: List<String> = listOf(
        // Sin equivalente local: el daemon guarda m_state.intensity (0..1) y el
        // motor local no tiene un único parámetro maestro de intensidad.
        // Espejarlo exige decidir qué significa "intensidad" en Ruta A
        // (wet/dry maestro o escala de todos los módulos), no solo cablear.
        "Intensidad global del motor",
        // Límite inherente a la captura: Ruta A solo procesa lo que la captura
        // entrega (audio que suena y permite captura). Sin daemon no hay
        // procesado a nivel sistema de todas las apps. Lo que SÍ se garantiza
        // es que TODO el DSP aplica al audio capturado.
        "Procesado del audio de todas las apps (sin root solo se procesa lo que la captura permite)"
    )

    /**
     * Texto para la UI; null = no hay nada que advertir en este modo.
     * @param daemonAlive si el daemon responde por socket. En ROOT_NO_DAEMON
     * con daemon vivo, la causa no es que el módulo no responda sino que el
     * efecto del sistema no está creado/procesando (Ruta B sin confirmar):
     * el título lo dice así para no mentir al usuario.
     */
    fun limitsFor(mode: AudioBackendSelector.Mode, daemonAlive: Boolean = false): Pair<String, List<String>>? = when (mode) {
        AudioBackendSelector.Mode.NO_ROOT ->
            "Modo sin root" to daemonOnlyFeatures
        AudioBackendSelector.Mode.ROOT_NO_DAEMON ->
            if (daemonAlive)
                "Ruta B sin confirmar: el daemon responde pero el efecto del sistema no está procesando" to daemonOnlyFeatures
            else
                "Root sin daemon: el módulo Magisk no responde" to daemonOnlyFeatures
        AudioBackendSelector.Mode.ROOT_DAEMON, AudioBackendSelector.Mode.UNKNOWN -> null
    }
}
