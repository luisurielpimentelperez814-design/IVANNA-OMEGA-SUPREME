package com.ivanna.omega.audio.engine

import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.sin

/**
 * ProfessionalAntiPopEngine — Sistema Anti-Pop y Continuidad de Fase de Nivel Profesional.
 *
 * Misión:
 * - Eliminar los chasquidos, pops y golpes de DC en cambios de estado dinámico:
 *     * Activación / desactivación de efectos (Cinematic, Spatial, NPE, Vibratory).
 *     * Cambio de modos (SCIFI, COSMIC, HORROR, VOID, NONE).
 *     * Cambios bruscos de ganancia o perfiles de ecualización.
 * - Filtro DC-Blocker de 5Hz de fase lineal en banda audible:
 *     * Elimina componentes continuas acumuladas por saturadores no lineales y excitadores,
 *       impidiendo que un paso de nivel DC sacuda el cono del altavoz.
 * - Detección de discontinuidades entre bloques:
 *     * Compara los bordes de bloque (|x[0] - x_prev[N-1]|) y aplica rampa de
 *       atenuación de borde si se detecta un salto de derivada pronunciado.
 * - Crossfade sinusoidal de igual potencia:
 *     * w_in(t) = sin(pi/2 * t/N), w_out(t) = cos(pi/2 * t/N)
 *     * Conservación estricta de energía acústica (w_in^2 + w_out^2 = 1.0).
 *
 * Cero asignaciones en el hot path.
 */
class ProfessionalAntiPopEngine(
    val sampleRate: Int = 48000
) {
    companion object {
        // Umbral sobre la segunda diferencia (error de predicción lineal C1),
        // evitando falsos positivos en señales agudas normales (>1 kHz) que antes
        // disparaban la rampa en cada bloque (150 Hz = estática/tronido continuo).
        private const val DISCONTINUITY_THRESHOLD = 0.75f
        private const val SMOOTH_FRAMES = 4
    }

    // Coeficiente DC blocker: R = 1 - (2 * pi * fc / fs)
    // Para fc = 5.0 Hz a 48kHz: R ≈ 0.999346
    private val dcCoeff: Float = (1.0 - (2.0 * PI * 5.0 / sampleRate)).toFloat().coerceIn(0.990f, 0.9999f)

    // Estados de memoria del DC Blocker
    private var dcPrevInL  = 0f
    private var dcPrevOutL = 0f
    private var dcPrevInR  = 0f
    private var dcPrevOutR = 0f

    // Memoria de los últimos 2 frames reproducidos para predicción C1 de derivada
    private var lastSampleL = 0f
    private var lastSampleR = 0f
    private var lastSlopeL  = 0f
    private var lastSlopeR  = 0f
    private var hasHistory  = false

    // Contador de intervenciones para telemetría
    @Volatile var antiPopInterventions: Int = 0
        private set

    val smoothedEventsTotal: Int
        get() = antiPopInterventions

    /**
     * Procesa el bloque de audio estéreo intercalado [L, R, L, R...]:
     * 1. Detecta únicamente saltos de escalón DC/fase reales (error de predicción C1 > 0.75).
     * 2. Aplica el filtro DC-Blocker de 5Hz y sanea NaN/Inf en tiempo real.
     * 3. Registra el estado final y pendiente para el próximo bloque.
     */
    fun process(buffer: FloatArray, frames: Int) {
        val samples = frames * 2
        if (samples <= 0 || samples > buffer.size) return

        // 1. Filtro DC-Blocker (5 Hz) + Saneo NaN/Inf: y[n] = x[n] - x[n-1] + R * y[n-1]
        var xL_prev = dcPrevInL
        var yL_prev = dcPrevOutL
        var xR_prev = dcPrevInR
        var yR_prev = dcPrevOutR
        val R = dcCoeff

        for (i in 0 until frames) {
            val idxL = i * 2
            val idxR = idxL + 1

            val rawL = buffer[idxL]
            val rawR = buffer[idxR]
            val inL = if (rawL.isFinite()) rawL else 0f
            val inR = if (rawR.isFinite()) rawR else 0f

            // FIX (distorsion digital en picos): el recorte duro a +-1.0 aqui clipeaba el
            // overshoot del DC-blocker y picos float >1 de la fuente (armonicos impares).
            // Solo se acota a +-4.0 (guarda anti-explosion); el limitador final decide el techo.
            val outL = (inL - xL_prev + R * yL_prev).coerceIn(-4.0f, 4.0f)
            val outR = (inR - xR_prev + R * yR_prev).coerceIn(-4.0f, 4.0f)

            buffer[idxL] = outL
            buffer[idxR] = outR

            xL_prev = inL
            yL_prev = outL
            xR_prev = inR
            yR_prev = outR
        }

        dcPrevInL  = xL_prev
        dcPrevOutL = yL_prev
        dcPrevInR  = xR_prev
        dcPrevOutR = yR_prev

        // 2. Detección de discontinuidad real de escalón (C1 prediction error)
        if (hasHistory && frames >= SMOOTH_FRAMES) {
            val expectedL = (lastSampleL + lastSlopeL).coerceIn(-1.0f, 1.0f)
            val expectedR = (lastSampleR + lastSlopeR).coerceIn(-1.0f, 1.0f)
            val errL = abs(buffer[0] - expectedL)
            val errR = abs(buffer[1] - expectedR)

            if (errL > DISCONTINUITY_THRESHOLD || errR > DISCONTINUITY_THRESHOLD) {
                antiPopInterventions++
                val smoothCount = minOf(SMOOTH_FRAMES, frames)
                for (f in 0 until smoothCount) {
                    val alpha = (f + 1).toFloat() / (smoothCount + 1).toFloat()
                    buffer[f * 2]     = expectedL * (1f - alpha) + buffer[f * 2] * alpha
                    buffer[f * 2 + 1] = expectedR * (1f - alpha) + buffer[f * 2 + 1] * alpha
                }
            }
        }

        // 3. Guardar último frame y pendiente para el siguiente bloque
        val endL = buffer[(frames - 1) * 2]
        val endR = buffer[(frames - 1) * 2 + 1]
        if (frames >= 2) {
            val prevEndL = buffer[(frames - 2) * 2]
            val prevEndR = buffer[(frames - 2) * 2 + 1]
            lastSlopeL = endL - prevEndL
            lastSlopeR = endR - prevEndR
        } else {
            lastSlopeL = 0f
            lastSlopeR = 0f
        }
        lastSampleL = endL
        lastSampleR = endR
        hasHistory = true
    }

    /**
     * Aplica un crossfade de igual potencia entre dos buffers estéreo preasignados.
     * out = prev * cos(theta) + next * sin(theta), donde theta va de 0 a pi/2.
     * Cero pérdida de energía acústica y continuidad de fase absoluta.
     */
    fun crossfadeEqualPower(
        sourceA: FloatArray,
        sourceB: FloatArray,
        destination: FloatArray,
        frames: Int
    ) {
        val total = minOf(frames, destination.size / 2, sourceA.size / 2, sourceB.size / 2)
        if (total <= 0) return

        val halfPi = (PI / 2.0).toFloat()
        val inv = 1.0f / total.toFloat()

        for (i in 0 until total) {
            val theta = (i * inv) * halfPi
            val wA = cos(theta)
            val wB = sin(theta)

            val idxL = i * 2
            val idxR = idxL + 1

            destination[idxL] = sourceA[idxL] * wA + sourceB[idxL] * wB
            destination[idxR] = sourceA[idxR] * wA + sourceB[idxR] * wB
        }
    }

    fun reset() {
        dcPrevInL = 0f
        dcPrevOutL = 0f
        dcPrevInR = 0f
        dcPrevOutR = 0f
        lastSampleL = 0f
        lastSampleR = 0f
        lastSlopeL = 0f
        lastSlopeR = 0f
        hasHistory = false
        antiPopInterventions = 0
    }
}
