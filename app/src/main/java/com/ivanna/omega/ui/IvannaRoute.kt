package com.ivanna.omega.ui

/**
 * IvannaRoute — centralized route name constants for navigation between
 * Compose destinations. This restores the symbol missing in recent refactors
 * and prevents "Unresolved reference: IvannaRoute" compiler errors in
 * MainActivity and other UI modules.
 */
object IvannaRoute {
    const val SPLASH = "splash"
    const val INTRO = "intro"
    const val DASHBOARD = "dashboard"

    const val MAGISK = "magisk"
    const val PROFILES = "profiles"
    const val VISUALIZER = "visualizer"

    const val ADAPTIVE = "adaptive"
    const val ADAPTIVE_DASH = "adaptive_dash"
    const val PERCEPTUAL = "perceptual_brain"

    const val OPE = "ope"
    const val BINAURAL = "binaural"
    const val AUDITORY = "auditory"
    const val LAB = "lab"

    const val SOUND = "sound"
    const val BRAIN = "brain"
    const val SPACE = "space"

    const val SYSTEM = "system"
    const val TELEMETRY = "telemetry"
}
