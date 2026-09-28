package com.ivanna.omega.audio.engine

/**
 * AudioThreadBudgetGuard — Presupuesto de Tiempo del Hilo de Audio y Protección de Deadline RT.
 *
 * Misión:
 * - Supervisar en nanosegundos (System.nanoTime) el tiempo consumido por cada bloque y módulo DSP.
 * - Conocer el presupuesto físico disponible: a 48kHz con 320 frames = 6.66 ms de periodo de bloque.
 * - Umbral de seguridad acústica: 70% del periodo (~4.66 ms) para garantizar que el hilo nunca
 *   se acerque al deadline del kernel ni del scheduler de AudioFlinger.
 * - Si el tiempo acumulado en el bloque amenaza con exceder el presupuesto seguro:
 *     * Activa una degradación controlada no audible (bypass de módulos secundarios intensivos,
 *       como convoluciones secundarias o reverb densa para ese bloque puntual).
 *     * El audio SIEMPRE tiene prioridad de continuidad: jamás permitir un underrun.
 * - Expone el porcentaje de carga DSP real (0% a 100%) y el conteo de intervenciones de guarda
 *   para la telemetría en tiempo real.
 *
 * Cero asignaciones en el hot path.
 */
class AudioThreadBudgetGuard {

    enum class BudgetStage {
        DSP_BRIDGE,
        CINEMATIC_ENGINE,
        SPATIAL_AUDIO,
        VIBRATORY_NPE,
        OUTPUT_STAGE
    }

    companion object {
        private const val SAFETY_MARGIN_RATIO = 0.70f // 70% del tiempo de bloque
        private const val DEGRADATION_TRIGGER_RATIO = 0.75f // 75% del presupuesto de seguridad
    }

    private var blockStartNs = 0L
    private var blockPeriodNs = 6_666_666L // Default 320 frames @ 48kHz
    private var safetyBudgetNs = 4_666_666L

    @Volatile var totalBlockTimeNs: Long = 0L
        private set

    @Volatile var dspLoadPercent: Float = 0f
        private set

    @Volatile var peakLoadPercent: Float = 0f
        private set

    @Volatile var budgetBypasses: Int = 0
        private set

    val bypassEventsTotal: Int
        get() = budgetBypasses

    @Volatile var isDegradingActive: Boolean = false
        private set

    @PublishedApi
    internal val stageTimesNs = LongArray(BudgetStage.values().size)

    /**
     * Inicia el cronómetro del bloque de audio.
     */
    fun startBlock(frames: Int, sampleRate: Int) {
        blockPeriodNs = (frames.toLong() * 1_000_000_000L) / sampleRate.coerceAtLeast(8000)
        safetyBudgetNs = (blockPeriodNs * SAFETY_MARGIN_RATIO).toLong()
        blockStartNs = System.nanoTime()
        isDegradingActive = false
    }

    /**
     * Evalúa si una etapa puede ejecutarse o si debe ser omitida / degradada para proteger el deadline.
     */
    fun canExecute(stage: BudgetStage): Boolean {
        if (isDegradingActive) return false
        val elapsedNs = System.nanoTime() - blockStartNs
        val triggerNs = (safetyBudgetNs * DEGRADATION_TRIGGER_RATIO).toLong()
        if (elapsedNs > triggerNs) {
            isDegradingActive = true
            budgetBypasses++
            return false
        }
        return true
    }

    /**
     * Mide el tiempo de ejecución de una etapa DSP específica.
     * Cero GC.
     */
    inline fun measureStage(stage: BudgetStage, action: () -> Unit) {
        val t0 = System.nanoTime()
        try {
            action()
        } finally {
            val t1 = System.nanoTime()
            stageTimesNs[stage.ordinal] = t1 - t0
        }
    }

    /**
     * Finaliza la medición del bloque y calcula la telemetría de carga.
     */
    fun finishBlock() {
        val endNs = System.nanoTime()
        totalBlockTimeNs = endNs - blockStartNs

        val instantLoad = (totalBlockTimeNs.toFloat() / blockPeriodNs.toFloat()) * 100f
        dspLoadPercent = (0.90f * dspLoadPercent) + (0.10f * instantLoad)

        if (dspLoadPercent > peakLoadPercent) {
            peakLoadPercent = dspLoadPercent
        }
    }

    fun getStageTimeNs(stage: BudgetStage): Long {
        return stageTimesNs[stage.ordinal]
    }

    fun reset() {
        totalBlockTimeNs = 0L
        dspLoadPercent = 0f
        peakLoadPercent = 0f
        budgetBypasses = 0
        isDegradingActive = false
        stageTimesNs.fill(0L)
    }
}
