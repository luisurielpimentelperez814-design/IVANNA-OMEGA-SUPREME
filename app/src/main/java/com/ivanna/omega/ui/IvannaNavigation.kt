package com.ivanna.omega.ui

/**
 * IvannaNavigation.kt — Mapa único de rutas de IVANNA OMEGA SUPREME.
 *
 * De 14 rutas dispersas → 5 secciones coherentes.
 * Cada sección agrupa funciones relacionadas sin perder ninguna funcionalidad.
 *
 * Árbol de navegación:
 *   splash → intro → dashboard
 *                     ├── sound      (EQ, Compresor, Exciter, Binaural, HRTF)
 *                     ├── brain      (Adaptive, Perceptual, Evolutivo, Lab)
 *                     ├── space      (Visualizer, Immersive3D, Auditory)
 *                     └── system     (Magisk, Profiles, Telemetría)
 */
object IvannaRoute {
    // Flujo de onboarding
    const val SPLASH  = "splash"
    const val INTRO   = "intro"

    // Hub principal
    const val DASHBOARD = "dashboard"

    // Sección SOUND — procesamiento de audio
    const val SOUND   = "sound"          // EQ + Compresor + Exciter
    const val BINAURAL = "binaural"      // HRTF + ITD/ILD + Spatial Width

    // Sección BRAIN — inteligencia adaptativa
    const val BRAIN       = "brain"          // hub de la sección
    const val ADAPTIVE    = "adaptive"       // AdaptiveEngine + modo manual
    const val PERCEPTUAL  = "perceptual"     // PerceptualBrain + ISO226
    const val LAB         = "lab"            // IvannaLab + evolutivo

    // Sección SPACE — visualización
    const val SPACE       = "space"
    const val VISUALIZER  = "visualizer"
    const val AUDITORY    = "auditory"

    // Sección SYSTEM — sistema y módulo
    const val SYSTEM      = "system"
    const val MAGISK      = "magisk"
    const val PROFILES    = "profiles"
    const val TELEMETRY   = "telemetry"

    // Rutas legacy mantenidas como aliases para no romper deep links existentes
    val LEGACY_ALIASES = mapOf(
        "ope"             to SOUND,
        "adaptive_dash"   to ADAPTIVE,
        "perceptual_brain"to PERCEPTUAL,
        "adaptive_profiles" to PROFILES
    )
}

/**
 * Secciones del Bottom Navigation del Dashboard.
 * Cada sección tiene su propio sub-NavHost interno.
 */
enum class DashboardSection(
    val route: String,
    val label: String,
    val icon: String   // nombre del icono Material — se resuelve en el composable
) {
    SOUND    (IvannaRoute.SOUND,     "SONIDO",    "equalizer"),
    BRAIN    (IvannaRoute.BRAIN,     "CEREBRO",   "psychology"),
    SPACE    (IvannaRoute.SPACE,     "ESPACIO",   "spatial_audio"),
    SYSTEM   (IvannaRoute.SYSTEM,    "SISTEMA",   "settings_applications")
}
