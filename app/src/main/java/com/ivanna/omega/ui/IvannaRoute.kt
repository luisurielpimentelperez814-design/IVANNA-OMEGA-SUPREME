package com.ivanna.omega.ui

/**
 * IvannaRoute — centraliza las rutas usadas por la navegación para evitar
 * literales esparcidas que provocan errores y dificultan refactors.
 */
object IvannaRoute {

    const val SPLASH = "splash"
    const val INTRO = "intro"
    const val DASHBOARD = "dashboard"

    // Sonido
    const val SOUND = "sound"
    const val OPE = "ope"
    const val BINAURAL = "binaural"

    // Cerebro / Perceptual
    const val BRAIN = "perceptual_brain"
    const val PERCEPTUAL = "perceptual_brain"
    const val ADAPTIVE = "adaptive"
    // MAGISTRAL — dashboard cognitivo (MagistralDashboardScreen). Ruta propia:
    // BRAIN ("perceptual_brain") ya la ocupa BrainScreen y no se toca.
    const val MAGISTRAL = "magistral"
    const val ADAPTIVE_DASH = "adaptive_dash"
    const val ADAPTIVE_PROFILES = "adaptive_profiles"
    const val LAB = "lab"

    // Espacio / Auditory
    const val SPACE = "space"
    const val AUDITORY = "auditory"
    const val VISUALIZER = "visualizer"

    // Sistema
    const val SYSTEM = "system"
    const val MAGISK = "magisk"
    const val PROFILES = "profiles"
    const val TELEMETRY = "telemetry"

    // Legacy / aliases
    const val OPE_ALIAS = "ope"
    const val BINAURAL_ALIAS = "binaural"
}
