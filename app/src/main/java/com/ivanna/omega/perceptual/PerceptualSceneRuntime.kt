package com.ivanna.omega.perceptual

import android.content.Context
import android.media.AudioManager
import com.ivanna.omega.audio.AudioRouteManager
import com.ivanna.omega.audio.BassHaptics
import com.ivanna.omega.audio.OutputRoute as AudioOutputRoute
import com.ivanna.omega.core.NativeBridge
import com.ivanna.omega.spatial.IvannaSpatialManager
import java.util.UUID

/**
 * Live adapter from app runtime read-backs to the versioned 10-axis contract.
 * It intentionally marks incomplete capabilities DEGRADED/UNAVAILABLE instead
 * of turning a request or a well-formed object into a false success claim.
 */
object PerceptualSceneRuntime {
    fun capture(context: Context): PerceptualSceneSnapshot {
        val now = System.currentTimeMillis()
        val app = context.applicationContext
        val audioManager = app.getSystemService(Context.AUDIO_SERVICE) as? AudioManager
        val route = audioManager?.let { AudioRouteManager.detectOutputRoute(it) } ?: AudioOutputRoute.UNKNOWN
        val pose = IvannaSpatialManager.scenePoseStatus()
        val movement = IvannaSpatialManager.movementStatus()
        val reverb = IvannaSpatialManager.reverbReadback()
        val haptics = BassHaptics.syncStatus()
        val hrtfSubject = IvannaSpatialManager.currentHrtfSubject()
        // This JNI getter reads the exact HybridRenderer used by the active app
        // spatial render path, not its separately owned control-pipeline sibling.
        val hybridTelemetry = NativeBridge.safeGetHybridMagistralTelemetry()
        val hybridTelemetryAvailable = hybridTelemetry.size >= 8 &&
            (0 until 8).all { hybridTelemetry[it].isFinite() }
        val hybridFallback = NativeBridge.safeIsHybridMagistralUsingFallbackHrtf()
        return assemble(
            PerceptualRuntimeEvidence(
                capturedAtEpochMs = now,
                outputRoute = route.toPerceptualRoute(),
                poseConfirmed = pose.engineConfirmed,
                requestedAzimuthRad = pose.requestedAzimuthRad,
                effectiveAzimuthRad = pose.effectiveAzimuthRad,
                requestedElevationRad = pose.requestedElevationRad,
                effectiveElevationRad = pose.effectiveElevationRad,
                requestedDistanceScale = pose.requestedDistanceScale,
                effectiveDistanceScale = pose.effectiveDistanceScale,
                poseUpdatedAtEpochMs = pose.updatedAtEpochMs,
                movementActive = movement.active,
                movementConfirmed = movement.latestTickConfirmed,
                movementUpdatedAtEpochMs = movement.updatedAtEpochMs,
                headRotationFresh = IvannaSpatialManager.isHeadTrackingFresh(),
                hrtfConfirmed = IvannaSpatialManager.isHrtfConfirmed(),
                hrtfSubject = hrtfSubject,
                reverbRequestedLevel = reverb.requestedLevel,
                reverbEffectiveLevel = reverb.effectiveLevel,
                reverbConfirmed = reverb.engineConfirmed,
                measuredRirActive = reverb.measuredRirActive,
                measuredRt60Seconds = reverb.measuredRt60Seconds,
                heuristicUpmixerEnabled = IvannaSpatialManager.isHeuristicStemSeparationConfirmed(),
                hapticsEnabled = haptics.enabled,
                hapticDelayMs = haptics.effectiveDelayMs,
                hapticLatencyMeasured = haptics.latencyMeasuredForCurrentRoute,
                hapticRouteDescription = haptics.routeDescription,
                hybridTelemetryAvailable = hybridTelemetryAvailable,
                hybridEnabled = hybridTelemetryAvailable && hybridTelemetry[0] >= 0.5f,
                hybridWet = hybridTelemetry.getOrNull(1)?.takeIf { it.isFinite() },
                hybridAzimuthDeg = hybridTelemetry.getOrNull(2)?.takeIf { it.isFinite() },
                hybridElevationDeg = hybridTelemetry.getOrNull(3)?.takeIf { it.isFinite() },
                hybridRoomSize = hybridTelemetry.getOrNull(4)?.takeIf { it.isFinite() },
                hybridAbsorption = hybridTelemetry.getOrNull(5)?.takeIf { it.isFinite() },
                hybridDampening = hybridTelemetry.getOrNull(6)?.takeIf { it.isFinite() },
                hybridRoomWetMix = hybridTelemetry.getOrNull(7)?.takeIf { it.isFinite() },
                hybridUsingFallbackHrtf = hybridFallback
            )
        )
    }

    internal fun assemble(e: PerceptualRuntimeEvidence): PerceptualSceneSnapshot {
        val now = e.capturedAtEpochMs
        fun status(
            axis: PerceptualAxis,
            state: AxisState,
            unit: String,
            requested: Float? = null,
            effective: Float? = null,
            reason: String? = null,
            observedAt: Long = now,
            engineConfirmed: Boolean = false
        ) = AxisStatus(
            axis = axis,
            state = state,
            unit = unit,
            requestedValue = requested,
            effectiveValue = effective,
            engineConfirmed = engineConfirmed,
            reason = reason,
            updatedAtEpochMs = observedAt.takeIf { it > 0L }
        )

        val hybridSpatialActive = e.hybridTelemetryAvailable && e.hybridEnabled &&
            (e.hybridWet ?: 0f) > 0.001f
        val hybridAzimuthRad = e.hybridAzimuthDeg?.let { Math.toRadians(it.toDouble()).toFloat() }
        val hybridElevationRad = e.hybridElevationDeg?.let { Math.toRadians(it.toDouble()).toFloat() }
        val hybridAzimuthMatches = hybridSpatialActive && hybridAzimuthRad != null &&
            kotlin.math.abs(hybridAzimuthRad - e.requestedAzimuthRad) <= 0.02f
        val hybridElevationMatches = hybridSpatialActive && hybridElevationRad != null &&
            kotlin.math.abs(hybridElevationRad - e.requestedElevationRad) <= 0.02f
        val managerPoseConfirmed = e.poseConfirmed && e.effectiveAzimuthRad != null
        val managerAzimuthMatches = managerPoseConfirmed &&
            kotlin.math.abs(e.effectiveAzimuthRad!! - e.requestedAzimuthRad) <= 0.02f
        val azimuthConfirmed = hybridAzimuthMatches ||
            (managerAzimuthMatches && !hybridSpatialActive)
        // Vertical source placement alone is not equivalent to an elevated HRTF.
        // Promote elevation to APPLIED only when measured/non-fallback HRTF filters
        // and the actual rendered hybrid angle agree with the requested value.
        val elevationHrtfConfirmed = hybridElevationMatches && !e.hybridUsingFallbackHrtf
        val elevationConfirmed = elevationHrtfConfirmed
        val distanceConfirmed = e.poseConfirmed && e.effectiveDistanceScale != null
        val movementConfirmed = e.movementActive && e.movementConfirmed
        val managerHrtfConfirmed = e.hrtfConfirmed && !e.hrtfSubject.isNullOrBlank() && e.hrtfSubject != "none"
        val measuredHybridHrtfActive = hybridSpatialActive && !e.hybridUsingFallbackHrtf
        val hrtfAvailable = managerHrtfConfirmed || measuredHybridHrtfActive
        val hybridRoomWetActive = e.hybridTelemetryAvailable && e.hybridEnabled &&
            (e.hybridWet ?: 0f) > 0.001f && (e.hybridRoomWetMix ?: 0f) > 0.001f

        val axes = listOf(
            status(
                PerceptualAxis.AZIMUTH,
                when {
                    azimuthConfirmed -> AxisState.APPLIED
                    managerPoseConfirmed -> AxisState.DEGRADED
                    else -> AxisState.UNAVAILABLE
                },
                "rad", e.requestedAzimuthRad,
                if (hybridSpatialActive) hybridAzimuthRad else e.effectiveAzimuthRad,
                when {
                    azimuthConfirmed -> null
                    managerPoseConfirmed && hybridSpatialActive ->
                        "La geometría de objetos sí se leyó, pero el azimut del HybridRenderer activo no coincide con el solicitado"
                    managerPoseConfirmed -> "La posición relativa de los objetos se leyó del renderer"
                    else -> "Ningún renderer activo confirmó el azimut solicitado"
                },
                e.poseUpdatedAtEpochMs,
                engineConfirmed = azimuthConfirmed
            ),
            status(
                PerceptualAxis.ELEVATION,
                when {
                    elevationHrtfConfirmed -> AxisState.APPLIED
                    managerPoseConfirmed || hybridSpatialActive -> AxisState.DEGRADED
                    else -> AxisState.UNAVAILABLE
                },
                "rad", e.requestedElevationRad,
                if (hybridSpatialActive) hybridElevationRad else e.effectiveElevationRad,
                when {
                    elevationHrtfConfirmed -> null
                    hybridSpatialActive && e.hybridUsingFallbackHrtf ->
                        "El ángulo vertical está leído de vuelta, pero el HybridRenderer confirma HRTF de respaldo, no filtros medidos"
                    managerPoseConfirmed ->
                        "La posición vertical de los objetos se leyó de vuelta; el renderer de objetos no confirma por sí solo una respuesta HRTF elevada"
                    else -> "No hay filtro HRTF de elevación activo confirmado"
                },
                engineConfirmed = elevationHrtfConfirmed
            ),
            status(
                PerceptualAxis.DISTANCE,
                if (distanceConfirmed) AxisState.DEGRADED else AxisState.UNAVAILABLE,
                "scale", e.requestedDistanceScale, e.effectiveDistanceScale,
                if (distanceConfirmed)
                    "Aplicada posición relativa y ganancia acotada; no equivale a distancia física calibrada en metros"
                else "No hay posición y atenuación relativa de distancia confirmadas"
            ),
            status(
                PerceptualAxis.MOVEMENT,
                if (movementConfirmed) AxisState.APPLIED else AxisState.UNAVAILABLE,
                "active", if (e.movementActive) 1f else 0f, if (movementConfirmed) 1f else 0f,
                if (movementConfirmed) null else "No hay trayectoria de fuente activa con confirmación reciente del renderer",
                e.movementUpdatedAtEpochMs,
                engineConfirmed = movementConfirmed
            ),
            status(
                PerceptualAxis.HEAD_ROTATION,
                if (e.headRotationFresh) AxisState.DEGRADED else AxisState.UNAVAILABLE,
                "fresh-sensor", if (e.headRotationFresh) 1f else 0f, if (e.headRotationFresh) 1f else 0f,
                if (e.headRotationFresh)
                    "El sensor real llega a JNI; la ruta actual reorienta el azimut, no confirma seguimiento HRTF completo de 6DoF"
                else "No hay muestra reciente del sensor de rotación confirmada en JNI"
            ),
            status(
                PerceptualAxis.HRTF_PERSONALIZATION,
                if (hrtfAvailable) AxisState.DEGRADED else AxisState.UNAVAILABLE,
                "subject-id",
                if (managerHrtfConfirmed) 1f else 0f,
                if (hrtfAvailable) 1f else 0f,
                when {
                    managerHrtfConfirmed ->
                        "Sujeto HRTF leído del ObjectRenderer (" + e.hrtfSubject + "); falta procedencia/calibración antropométrica confirmada para llamarlo personalización completa"
                    measuredHybridHrtfActive ->
                        "El HybridRenderer activo usa filtros no-fallback, pero no confirma una personalización HRTF individual del oyente"
                    else -> "No se confirmó HRTF medido activo ni un sujeto HRTF cargado"
                }
            ),
            status(
                PerceptualAxis.ROOM_GEOMETRY,
                if (e.hybridTelemetryAvailable && e.hybridRoomSize != null) AxisState.DEGRADED else AxisState.UNAVAILABLE,
                "normalized-room-size", e.hybridRoomSize, e.hybridRoomSize,
                if (e.hybridTelemetryAvailable && e.hybridRoomSize != null)
                    "El tamaño relativo, absorción y amortiguamiento están leídos del HybridRenderer; no publica ancho, profundidad, altura ni posición del oyente medidos en metros"
                else if (e.measuredRirActive)
                    "Hay RIR medida activa, pero no se publican dimensiones y posición del oyente de una sala calibrada"
                else "No hay geometría de sala ni tamaño relativo confirmados por lectura del renderer"
            ),
            status(
                PerceptualAxis.REVERBERANT_FIELD,
                when {
                    e.reverbConfirmed && e.reverbEffectiveLevel != null &&
                        e.measuredRirActive && e.measuredRt60Seconds != null -> AxisState.APPLIED
                    e.reverbConfirmed && e.reverbEffectiveLevel != null || hybridRoomWetActive -> AxisState.DEGRADED
                    else -> AxisState.UNAVAILABLE
                },
                "wet 0..1", e.reverbRequestedLevel, e.reverbEffectiveLevel,
                when {
                    e.reverbConfirmed && e.reverbEffectiveLevel != null &&
                        e.measuredRirActive && e.measuredRt60Seconds != null -> null
                    e.reverbConfirmed && e.reverbEffectiveLevel != null && hybridRoomWetActive ->
                        "El nivel nativo fue leído; también está activo el modelo Schroeder/Moorer del HybridRenderer, pero no hay RIR medida confirmada en el renderer de objetos"
                    e.reverbConfirmed && e.reverbEffectiveLevel != null ->
                        "El nivel de reverberación fue leído, pero no hay una RIR medida activa confirmada"
                    hybridRoomWetActive ->
                        "El modelo de sala sintético está activo por telemetría; falta confirmación nativa del nivel/RIR medida"
                    else -> "No se confirmó nivel de reverberación ni una sala húmeda activa"
                },
                engineConfirmed = e.reverbConfirmed && e.reverbEffectiveLevel != null &&
                    e.measuredRirActive && e.measuredRt60Seconds != null
            ),
            status(
                PerceptualAxis.SOURCE_SEPARATION,
                if (e.heuristicUpmixerEnabled) AxisState.DEGRADED else AxisState.UNAVAILABLE,
                "heuristic-4-stem", if (e.heuristicUpmixerEnabled) 1f else 0f,
                if (e.heuristicUpmixerEnabled) 1f else 0f,
                if (e.heuristicUpmixerEnabled)
                    "Cuatro stems habilitados en el upmixer nativo; separación espectral heurística, no aislamiento semántico de instrumentos"
                else "El upmixer de cuatro stems no está habilitado o no pudo leerse"
            ),
            status(
                PerceptualAxis.AV_HAPTIC_SYNC,
                if (e.hapticsEnabled) AxisState.DEGRADED else AxisState.UNAVAILABLE,
                "ms", e.hapticDelayMs.toFloat(), if (e.hapticsEnabled) e.hapticDelayMs.toFloat() else null,
                if (e.hapticsEnabled) {
                    if (e.hapticLatencyMeasured)
                        "Háptica activa; la lectura de latencia de la ruta actual incluye latencia del micrófono y no demuestra sincronía exacta extremo a extremo (" + e.hapticRouteDescription + ")"
                    else
                        "Háptica activa; el retardo usa estimación de ruta/motor, no una medición sincronizada extremo a extremo (" + e.hapticRouteDescription + ")"
                } else "El motor háptico no está activo; no se declara sincronía A/V aplicada"
            )
        )

        return PerceptualSceneSnapshot(
            sceneId = "ivanna-10d-" + UUID.randomUUID(),
            capturedAtEpochMs = now,
            outputRoute = e.outputRoute,
            // El renderer actual usa coordenadas relativas, no metros medidos.
            sources = emptyList(),
            axes = axes,
            room = null,
            hrtf = null,
            calibration = SceneCalibration()
        )
    }

    private fun AudioOutputRoute.toPerceptualRoute(): OutputRoute = when (this) {
        AudioOutputRoute.BLUETOOTH -> OutputRoute.HEADPHONES
        AudioOutputRoute.WIRED_AUX -> OutputRoute.AUX
        AudioOutputRoute.USB -> OutputRoute.USB_DAC
        AudioOutputRoute.SPEAKER -> OutputRoute.SPEAKERS
        AudioOutputRoute.UNKNOWN -> OutputRoute.UNKNOWN
    }
}

internal data class PerceptualRuntimeEvidence(
    val capturedAtEpochMs: Long,
    val outputRoute: OutputRoute,
    val poseConfirmed: Boolean,
    val requestedAzimuthRad: Float,
    val effectiveAzimuthRad: Float?,
    val requestedElevationRad: Float,
    val effectiveElevationRad: Float?,
    val requestedDistanceScale: Float,
    val effectiveDistanceScale: Float?,
    val poseUpdatedAtEpochMs: Long,
    val movementActive: Boolean,
    val movementConfirmed: Boolean,
    val movementUpdatedAtEpochMs: Long,
    val headRotationFresh: Boolean,
    val hrtfConfirmed: Boolean,
    val hrtfSubject: String?,
    val reverbRequestedLevel: Float,
    val reverbEffectiveLevel: Float?,
    val reverbConfirmed: Boolean,
    val measuredRirActive: Boolean,
    val measuredRt60Seconds: Float?,
    val heuristicUpmixerEnabled: Boolean,
    val hapticsEnabled: Boolean,
    val hapticDelayMs: Int,
    val hapticLatencyMeasured: Boolean,
    val hapticRouteDescription: String,
    val hybridTelemetryAvailable: Boolean = false,
    val hybridEnabled: Boolean = false,
    val hybridWet: Float? = null,
    val hybridAzimuthDeg: Float? = null,
    val hybridElevationDeg: Float? = null,
    val hybridRoomSize: Float? = null,
    val hybridAbsorption: Float? = null,
    val hybridDampening: Float? = null,
    val hybridRoomWetMix: Float? = null,
    val hybridUsingFallbackHrtf: Boolean = true
)
