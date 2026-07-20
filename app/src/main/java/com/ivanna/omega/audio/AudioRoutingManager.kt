package com.ivanna.omega.audio

import android.content.Context
import android.media.AudioTrack

/**
 * AudioRoutingManager \u2014 API hist\u00f3rica de forzado de ruta USB DAC.
 *
 * Este objeto fue creado antes de que existiera [AudioRouteManager] (el
 * detector/perfilador activo del pipeline). Su superficie p\u00fablica
 * (`forceUsbDacRouting`, `restoreDefaultRouting`) qued\u00f3 hu\u00e9rfana: ning\u00fan
 * consumidor Kotlin del proyecto la invoca. Sin embargo, borrar la clase
 * (a) romper\u00eda cualquier c\u00f3digo externo/experimental que la referencie
 * por reflexi\u00f3n y (b) descartar\u00eda el \u00fanico camino documentado del
 * proyecto para forzar el DAC USB.
 *
 * Regla de oro (no borramos \u2014 fusionamos): la l\u00f3gica real fue absorbida
 * por [AudioRouteManager], que adem\u00e1s publica el perfil DSP (USB) al
 * instante en vez de esperar al AudioDeviceCallback. Este objeto queda
 * como fachada delgada @Deprecated hacia esas mismas llamadas, para que
 * (1) la funcionalidad siga viva y (2) nadie duplique en el futuro la
 * pol\u00edtica de perfilado en dos sitios distintos.
 */
@Deprecated(
    message = "Usar AudioRouteManager.forceUsbDacRouting / restoreDefaultRouting. Esta clase se conserva como fachada de compatibilidad.",
    replaceWith = ReplaceWith(
        "AudioRouteManager.forceUsbDacRouting(context, audioTrack)",
        "com.ivanna.omega.audio.AudioRouteManager"
    )
)
object AudioRoutingManager {
    /** @see AudioRouteManager.forceUsbDacRouting */
    fun forceUsbDacRouting(context: Context, audioTrack: AudioTrack? = null): Boolean =
        AudioRouteManager.forceUsbDacRouting(context, audioTrack)

    /** @see AudioRouteManager.restoreDefaultRouting */
    fun restoreDefaultRouting(context: Context, audioTrack: AudioTrack? = null): Boolean =
        AudioRouteManager.restoreDefaultRouting(context, audioTrack)
}
