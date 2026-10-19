package com.ivanna.omega.safmus

/**
 * SAFMUS universal safety/decision kernel.
 *
 * Deterministic supervisory policy, not a trained model. It may authorize only
 * existing processing paths; it never performs DSP and never treats a request,
 * optimizer score, preset, or synthetic fallback as measured acoustic evidence.
 * Keep this class free of Android dependencies so JVM tests can exercise policy.
 */
object SafmusUniversalKernel {
    enum class Route { ROUTE_A, ROUTE_B, NONE }
    enum class EvidenceKind { MEASURED, SIMULATED, ESTIMATED, USER_DECLARED, UNKNOWN }

    data class RouteEvidence(
        val systemWideEffectActive: Boolean,
        val routeASignalValid: Boolean,
        val routeAFrameFresh: Boolean,
        val enabled: Boolean,
        val bypass: Boolean
    )

    data class RouteDecision(
        val route: Route,
        val allowSpectralAdaptation: Boolean,
        val reason: String
    )

    data class Gate(val allowed: Boolean, val reason: String)

    /** Reject non-finite control values and clamp valid input to the widget's declared range. */
    fun sanitizeControlValue(value: Float, min: Float, max: Float): Float? {
        if (!value.isFinite() || !min.isFinite() || !max.isFinite() || min > max) return null
        return value.coerceIn(min, max)
    }

    fun decideRoute(e: RouteEvidence): RouteDecision = when {
        e.systemWideEffectActive -> RouteDecision(
            Route.ROUTE_B, false,
            "La ruta global posee el DSP; SAFMUS bloquea procesamiento paralelo en Ruta A"
        )
        e.routeASignalValid && e.routeAFrameFresh && e.enabled && !e.bypass -> RouteDecision(
            Route.ROUTE_A, true,
            "Ruta A con señal válida y reciente; adaptación acotada permitida"
        )
        e.routeASignalValid && !e.routeAFrameFresh -> RouteDecision(
            Route.NONE, false,
            "Señal obsoleta: abstenerse y conservar el último estado seguro"
        )
        e.routeASignalValid -> RouteDecision(
            Route.ROUTE_A, false,
            if (e.bypass) "Bypass activo; conservar parámetros base"
            else "Adaptación desactivada; conservar parámetros base"
        )
        else -> RouteDecision(
            Route.NONE, false,
            "No hay ruta con evidencia válida; conservar el estado seguro"
        )
    }

    /** Dataset/pose preflight used before a subject load; headroom is checked by the DSP path. */
    fun gateHrtfDataset(
        binauralOutput: Boolean,
        coordinatesFinite: Boolean,
        datasetCompatible: Boolean
    ): Gate = when {
        !binauralOutput -> Gate(false, "La salida no es binaural")
        !coordinatesFinite -> Gate(false, "Coordenadas HRTF no finitas o fuera de rango")
        !datasetCompatible -> Gate(false, "Dataset HRTF incompatible; no declarar aplicación")
        else -> Gate(true, "Precondiciones de dataset HRTF satisfechas")
    }

    /** HRTF activation adds the separate measured headroom requirement. */
    fun gateHrtf(
        binauralOutput: Boolean,
        coordinatesFinite: Boolean,
        headroomValid: Boolean,
        datasetCompatible: Boolean
    ): Gate {
        val preflight = gateHrtfDataset(binauralOutput, coordinatesFinite, datasetCompatible)
        if (!preflight.allowed) return preflight
        return if (!headroomValid) Gate(false, "Headroom insuficiente")
        else Gate(true, "Condiciones de ruta HRTF satisfechas")
    }

    /** RIR configuration preflight; the caller separately owns real-time latency verification. */
    fun gateRirConfiguration(
        explicitlySelectedOrMeasured: Boolean,
        metadataCompatible: Boolean,
        sampleRateCompatible: Boolean
    ): Gate = when {
        !explicitlySelectedOrMeasured -> Gate(false, "No hay RIR medida o seleccionada explícitamente")
        !metadataCompatible -> Gate(false, "Metadatos RIR incompatibles")
        !sampleRateCompatible -> Gate(false, "Frecuencia de muestreo RIR incompatible")
        else -> Gate(true, "Precondiciones de configuración RIR satisfechas")
    }

    /** RIR activation additionally requires the processing latency budget to be verified. */
    fun gateRir(
        explicitlySelectedOrMeasured: Boolean,
        metadataCompatible: Boolean,
        sampleRateCompatible: Boolean,
        latencyWithinBudget: Boolean
    ): Gate {
        val preflight = gateRirConfiguration(
            explicitlySelectedOrMeasured, metadataCompatible, sampleRateCompatible
        )
        if (!preflight.allowed) return preflight
        return if (!latencyWithinBudget) Gate(false, "RIR excede el presupuesto de latencia")
        else Gate(true, "Condiciones de RIR satisfechas")
    }

    /** Room correction must not infer a physical room from genre, presets, or synthetic responses. */
    fun gateRoomCorrection(
        measurementAvailable: Boolean,
        playbackChainIdentified: Boolean,
        candidateFiniteAndStable: Boolean,
        headroomValid: Boolean
    ): Gate = when {
        !measurementAvailable -> Gate(false, "Corrección de sala requiere medición real")
        !playbackChainIdentified -> Gate(false, "Cadena de reproducción no identificada")
        !candidateFiniteAndStable -> Gate(false, "Candidato no finito o inestable")
        !headroomValid -> Gate(false, "Headroom insuficiente")
        else -> Gate(true, "Condiciones de corrección de sala satisfechas")
    }

    /**
     * Allows the UI to inspect a telemetry candidate; this is NOT activation approval.
     * Applying a candidate still requires [mayAcceptEvolutionProposal] and native read-back.
     */
    fun mayEvaluateEvolutionProposal(route: Route, signalValid: Boolean, fitness: Float): Boolean =
        route == Route.ROUTE_A && signalValid && fitness.isFinite()

    /**
     * Legacy Route-A evolution gate, strengthened with stability and latency checks.
     * Fitness is an optimizer metric, not proof of audible/perceptual improvement.
     */
    fun mayAcceptEvolutionProposal(
        route: Route,
        signalValid: Boolean,
        fitness: Float,
        headroomValid: Boolean,
        candidateFiniteAndStable: Boolean = true,
        latencyWithinBudget: Boolean = true
    ): Boolean = route == Route.ROUTE_A &&
        signalValid &&
        fitness.isFinite() &&
        headroomValid &&
        candidateFiniteAndStable &&
        latencyWithinBudget
}
