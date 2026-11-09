package com.ivanna.omega.ui

import android.content.Context

/**
 * IntroVideoGate — puerta de la intro de apertura de IVANNA (v2.5.371).
 *
 * Historia: la intro existe desde 2026-09-14 (commit 89f784e0, re-aplicada
 * en la línea actual): el video `res/raw/ivanna_intro.mp4` (10,0 s, 720×1280,
 * H.264 + AAC, 5,6 MB) y la pantalla `IntroVideoScreen` (VideoView nativo,
 * auto-salto al completar + botón SALTAR) están cableados en MainActivity
 * (NavHost: splash -> introVideo -> intro -> dashboard). Pero el video solo
 * se veía DESPUÉS de tocar "ACEPTAR E INICIAR" en el aviso legal del splash:
 * al instalar, lo primero que aparecía era el texto legal, no el video.
 *
 * Petición directa de Esse (2026-11-09, para la 371): "cuando se instale,
 * comience el video de intro". Esta puerta invierte el orden en el primer
 * arranque: el video es LO PRIMERO que se ve tras instalar; el aviso legal
 * viene después del video. En arranques posteriores el flujo vuelve al
 * normal (splash -> intro -> dashboard) y el video no se repite.
 *
 * Estado: se guarda en SharedPreferences ("ivanna_intro_video"), así que
 * sobrevive a reinicios de la app pero se borra al desinstalar (como debe
 * ser: al reinstalar, el video vuelve a ser lo primero).
 *
 * Integración en MainActivity.kt (documentada en
 * docs/ROCK_CONCERTS_INTRO_371.md; MainActivity NO se modifica aquí porque
 * otro subagente de la 371 la está tocando):
 *
 * ```
 * val introAlreadyShown = remember { IntroVideoGate.hasShownIntroVideo(context) }
 * NavHost(nav, startDestination = if (introAlreadyShown) "splash" else "introVideo") {
 *     composable("splash") {
 *         SplashScreen {
 *             nav.navigate(if (IntroVideoGate.hasShownIntroVideo(context)) "intro" else "introVideo")
 *         }
 *     }
 *     composable("introVideo") {
 *         IntroVideoScreen {
 *             IntroVideoGate.markIntroVideoShown(context)
 *             nav.navigate("splash") { popUpTo("introVideo") { inclusive = true } }
 *         }
 *     }
 *     // …resto sin cambios…
 * }
 * ```
 *
 * "Ver intro de nuevo" (opcional, p. ej. desde SystemScreen o Acerca de):
 * `IntroVideoGate.resetIntroVideo(context)` y navegar a "introVideo".
 */
object IntroVideoGate {

    /** Ruta del NavHost donde vive el video (ya existe en MainActivity). */
    const val ROUTE_INTRO_VIDEO = "introVideo"

    private const val PREFS = "ivanna_intro_video"
    private const val KEY_SHOWN = "intro_video_shown"

    /** true si el video de intro ya se mostró al menos una vez en este equipo. */
    fun hasShownIntroVideo(context: Context): Boolean =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .getBoolean(KEY_SHOWN, false)

    /**
     * true si el video debe ser lo PRIMERO que se vea en este arranque
     * (primer arranque tras instalar). En arranques posteriores, false.
     */
    fun shouldShowIntroVideoFirst(context: Context): Boolean =
        !hasShownIntroVideo(context)

    /** Marca el video como mostrado (llamar al terminar o saltar el video). */
    fun markIntroVideoShown(context: Context) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .edit()
            .putBoolean(KEY_SHOWN, true)
            .apply()
    }

    /** Olvida que el video se mostró: la próxima vez vuelve a ser lo primero. */
    fun resetIntroVideo(context: Context) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .edit()
            .remove(KEY_SHOWN)
            .apply()
    }
}
