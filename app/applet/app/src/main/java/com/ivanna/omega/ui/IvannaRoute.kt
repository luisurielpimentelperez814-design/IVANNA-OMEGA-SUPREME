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
    // FIX (pantalla muerta): PerceptualBrainDashboard (461 lineas, motor real +
    // sliders interactivos) estaba importada en MainActivity pero SIN destino en
    // el NavHost: era inalcanzable. Ruta propia, distinta de BRAIN (BrainScreen).
    const val PERCEPTUAL_CORTEX = "perceptual_cortex"
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
    const val ABX_TEST = "abx_test"
    const val AUDIO_CONTROL_HUB = "audio_control_hub"  // SOFA · AF · RIR · SAF panel

    // IVANNA Conversational Acoustic Intelligence (FASE 12–19)
    const val IVANNA_ASSISTANT = "ivanna_assistant"

    // Panel de conectividad WiFi / datos / Gemini Agent
    const val NETWORK = "network_status"

    // ── 5 Ejes de Supremacía Cuántico-Neuromórfica (Prompt Maestro 2026) ─────
    const val SUPREME_AXES_HUB       = "supreme_axes_hub"
    const val SUPREME_AXIS_1_LATTICE = "supreme_axis_1_lattice"
    const val SUPREME_AXIS_2_CVNN    = "supreme_axis_2_cvnn"
    const val SUPREME_AXIS_3_SNN_HOA = "supreme_axis_3_snn_hoa"
    const val SUPREME_AXIS_4_PINNA_INR  = "supreme_axis_4_pinna_inr"
    const val SUPREME_AXIS_5_SHM_FARROW = "supreme_axis_5_shm_farrow"
}
