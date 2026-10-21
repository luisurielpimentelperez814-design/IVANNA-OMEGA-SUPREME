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

    data class SpatialCaptureAssessment(
        val exportAllowed: Boolean,
        val quality: String,
        val reason: String,
        val evidenceKind: String = "ESTIMATED_AR_GEOMETRY",
        val acousticCalibrationReady: Boolean = false
    )

    data class SpatialPoint3(val x: Float, val y: Float, val z: Float)
    data class SpatialCloudOptimization(
        val points: List<SpatialPoint3>,
        val inputCount: Int,
        val rejectedInvalidCount: Int,
        val reducedByVoxelCount: Int,
        val truncatedCount: Int
    )

    /**
     * SAFMUS spatial preprocessing: discard non-finite/out-of-domain coordinates,
     * voxel-thin redundant samples, and enforce a hard output limit. This optimizes
     * geometry payload size without pretending to reconstruct unseen surfaces.
     */
    fun optimizeSpatialPointCloud(
        input: List<SpatialPoint3>,
        voxelSizeMeters: Float = 0.02f,
        maxOutputPoints: Int = 100_000
    ): SpatialCloudOptimization {
        val voxel = sanitizeControlValue(voxelSizeMeters, 0.005f, 0.25f) ?: 0.02f
        val limit = maxOutputPoints.coerceIn(1, 250_000)
        val output = ArrayList<SpatialPoint3>(minOf(input.size, limit))
        val occupied = HashSet<String>(minOf(input.size, limit))
        var invalid = 0
        var reduced = 0
        var truncated = 0
        for (point in input) {
            if (!point.x.isFinite() || !point.y.isFinite() || !point.z.isFinite() ||
                kotlin.math.abs(point.x) > 1_000f ||
                kotlin.math.abs(point.y) > 1_000f ||
                kotlin.math.abs(point.z) > 1_000f
            ) {
                invalid++
                continue
            }
            val gx = kotlin.math.floor(point.x / voxel).toLong()
            val gy = kotlin.math.floor(point.y / voxel).toLong()
            val gz = kotlin.math.floor(point.z / voxel).toLong()
            val key = "$gx:$gy:$gz"
            if (!occupied.add(key)) {
                reduced++
                continue
            }
            if (output.size >= limit) {
                truncated++
                continue
            }
            output.add(point)
        }
        return SpatialCloudOptimization(output, input.size, invalid, reduced, truncated)
    }

    /**
     * SAFMUS quality gate for ARCore geometry. AR points/poses are estimates,
     * not acoustic measurements; this gate never authorizes RIR/RT60 calibration.
     * Weak captures remain exportable with an explicit degraded-quality label.
     */
    fun assessSpatialCapture(
        pointCount: Int,
        depthPointCount: Int,
        sparsePointCount: Int,
        poseCount: Int,
        durationMs: Long,
        trackingState: String,
        coordinatesFinite: Boolean,
        posesFinite: Boolean
    ): SpatialCaptureAssessment {
        if (!coordinatesFinite || !posesFinite) {
            return SpatialCaptureAssessment(false, "REJECTED_INVALID", "SAFMUS rechazó coordenadas o poses no finitas")
        }
        if (pointCount <= 0) {
            return SpatialCaptureAssessment(false, "REJECTED_INCOMPLETE", "SAFMUS no permite exportar una nube vacía")
        }

        // Rechaza metadatos imposibles antes de etiquetar una captura como revisable.
        // El número final de puntos nunca puede exceder las muestras de origen registradas.
        if (depthPointCount < 0 || sparsePointCount < 0 || poseCount < 0 || durationMs < 0L) {
            return SpatialCaptureAssessment(false, "REJECTED_INVALID", "SAFMUS rechazó contadores o duración negativos")
        }
        val sourcePointCount = depthPointCount.toLong() + sparsePointCount.toLong()
        if (sourcePointCount < pointCount.toLong()) {
            return SpatialCaptureAssessment(false, "REJECTED_INVALID", "El conteo final supera las muestras de origen registradas")
        }
        val enoughSamples = pointCount >= 100 && poseCount >= 3 && durationMs >= 3_000L
        val trackingOk = trackingState == "TRACKING"
        val depthBacked = depthPointCount > 0
        val quality = when {
            !enoughSamples || !trackingOk -> "DEGRADED"
            depthBacked -> "REVIEWABLE_DEPTH_ESTIMATE"
            sparsePointCount > 0 -> "REVIEWABLE_VISUAL_ESTIMATE"
            else -> "DEGRADED"
        }
        val reason = when (quality) {
            "REVIEWABLE_DEPTH_ESTIMATE" -> "Captura con muestras de profundidad y seguimiento final activo; geometría estimada, no acústica"
            "REVIEWABLE_VISUAL_ESTIMATE" -> "Captura visual suficiente; geometría estimada, no acústica"
            else -> "Captura exportable con calidad limitada; mejora el barrido y confirma seguimiento"
        }
        return SpatialCaptureAssessment(true, quality, reason)
    }



    /** Hardware classes are capabilities, not permission to mutate a device by themselves. */
    enum class HardwareResource { CAMERA, MICROPHONE, AUDIO_OUTPUT }

    /**
     * Preflight evidence for a bounded optimization of an already-owned hardware session.
     * A missing permission, stale route, unsupported setting, unsafe budget or unavailable
     * rollback must fail closed. This policy deliberately contains no Android/HAL calls.
     */
    data class HardwareOptimizationEvidence(
        val capabilityDetected: Boolean,
        val permissionGranted: Boolean,
        val owningSessionConfirmed: Boolean,
        val telemetryFresh: Boolean,
        val configurationSupported: Boolean,
        val resourceBudgetValid: Boolean,
        val rollbackAvailable: Boolean
    )

    fun gateHardwareOptimization(
        resource: HardwareResource,
        evidence: HardwareOptimizationEvidence
    ): Gate = when {
        !evidence.capabilityDetected ->
            Gate(false, "${resource.name}: capacidad no detectada")
        !evidence.permissionGranted ->
            Gate(false, "${resource.name}: permiso ausente o revocado")
        !evidence.owningSessionConfirmed ->
            Gate(false, "${resource.name}: sesión propietaria no confirmada")
        !evidence.telemetryFresh ->
            Gate(false, "${resource.name}: telemetría obsoleta")
        !evidence.configurationSupported ->
            Gate(false, "${resource.name}: configuración no soportada por el dispositivo")
        !evidence.resourceBudgetValid ->
            Gate(false, "${resource.name}: presupuesto térmico/memoria/latencia inválido")
        !evidence.rollbackAvailable ->
            Gate(false, "${resource.name}: no existe reversión segura")
        else -> Gate(true, "${resource.name}: precondiciones SAFMUS satisfechas")
    }

    /** Postflight: a proposal is not applied until effective read-back matches. */
    fun confirmHardwareOptimization(preflight: Gate, effectiveReadbackMatches: Boolean): Gate = when {
        !preflight.allowed -> preflight
        !effectiveReadbackMatches -> Gate(false, "Read-back efectivo no coincide; revertir al estado seguro")
        else -> Gate(true, "Optimización confirmada por read-back efectivo")
    }


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
        candidateFiniteAndStable: Boolean = false,
        latencyWithinBudget: Boolean = false
    ): Boolean = route == Route.ROUTE_A &&
        signalValid &&
        fitness.isFinite() &&
        headroomValid &&
        candidateFiniteAndStable &&
        latencyWithinBudget
}
