package com.ivanna.omega.perceptual

import android.content.Context
import android.media.AudioManager
import com.ivanna.omega.audio.AudioRouteManager
import com.ivanna.omega.audio.BassHaptics
import com.ivanna.omega.audio.OutputRoute as AudioOutputRoute
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
                hapticRouteDescription = haptics.routeDescription
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
            observedAt: Long = now
        ) = AxisStatus(
            axis = axis,
            state = state,
            unit = unit,
            requestedValue = requested,
            effectiveValue = effective,
            engineConfirmed = state == AxisState.APPLIED,
            reason = reason,
            updatedAtEpochMs = observedAt.takeIf { it > 0L }
        )

        val azimuthConfirmed = e.poseConfirmed && e.effectiveAzimuthRad != null
        val elevationConfirmed = e.poseConfirmed && e.effectiveElevationRad != null
        val distanceConfirmed = e.poseConfirmed && e.effectiveDistanceScale != null
        val movementConfirmed = e.movementActive && e.movementConfirmed
        val hrtfConfirmed = e.hrtfConfirmed && !e.hrtfSubject.isNullOrBlank() && e.hrtfSubject != "none"

        val axes = listOf(
            status(
                PerceptualAxis.AZIMUTH,
                if (azimuthConfirmed) AxisState.APPLIED else AxisState.UNAVAILABLE,
                "rad", e.requestedAzimuthRad, e.effectiveAzimuthRad,
                if (azimuthConfirmed) null else "El renderer no confirmó el azimut del buffer de objetos activo",
                e.poseUpdatedAtEpochMs
            ),
            status(
                PerceptualAxis.ELEVATION,
                if (elevationConfirmed) AxisState.DEGRADED else AxisState.UNAVAILABLE,
                "rad", e.requestedElevationRad, e.effectiveElevationRad,
                if (elevationConfirmed)
                    "La geometría vertical se leyó del renderer; no existe confirmación de una respuesta HRTF medida individual por elevación"
                else "No hay coordenadas verticales confirmadas por el renderer"
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
                e.movementUpdatedAtEpochMs
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
                if (hrtfConfirmed) AxisState.DEGRADED else AxisState.UNAVAILABLE,
                "subject-id", if (hrtfConfirmed) 1f else 0f, if (hrtfConfirmed) 1f else 0f,
                if (hrtfConfirmed)
                    "Carga y sujeto activo leídos del motor (" + e.hrtfSubject + "); no se declara personalización completa porque este snapshot no incluye procedencia medida ni calibración antropométrica confirmada del usuario"
                else "No hay dataset HRTF cargado con sujeto activo confirmado"
            ),
            status(
                PerceptualAxis.ROOM_GEOMETRY,
                AxisState.UNAVAILABLE,
                "meters",
                reason = if (e.measuredRirActive)
                    "Hay RIR medida activa, pero este camino no publica dimensiones y posición del oyente de una sala calibrada"
                else "No hay geometría de sala medida y leída de vuelta; RT60 por sí solo no es geometría"
            ),
            status(
                PerceptualAxis.REVERBERANT_FIELD,
                when {
                    !e.reverbConfirmed || e.reverbEffectiveLevel == null -> AxisState.UNAVAILABLE
                    e.measuredRirActive && e.measuredRt60Seconds != null -> AxisState.APPLIED
                    else -> AxisState.DEGRADED
                },
                "wet 0..1", e.reverbRequestedLevel, e.reverbEffectiveLevel,
                when {
                    !e.reverbConfirmed || e.reverbEffectiveLevel == null ->
                        "El nivel de reverberación no fue confirmado por lectura nativa"
                    e.measuredRirActive && e.measuredRt60Seconds != null -> null
                    else -> "El nivel fue leído de vuelta, pero se usa el FDN de respaldo y no una RIR medida activa"
                }
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
    val hapticRouteDescription: String
)
