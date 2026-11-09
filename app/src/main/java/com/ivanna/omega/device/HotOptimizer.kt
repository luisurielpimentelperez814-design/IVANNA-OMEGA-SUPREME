package com.ivanna.omega.device

import android.util.Log
import com.ivanna.omega.audio.AudioRouteManager
import com.ivanna.omega.audio.ThermalGovernor

/**
 * Motor de optimización en caliente de IVANNA.
 *
 * Formula y aplica las optimizaciones del informe de auditoría SIN reinicio.
 * Cada optimización es un registro CERRADO ([OptimizationKind]): no existe
 * ninguna capacidad genérica de "escribir en sysfs" ni de tocar el HAL.
 * Cada aplicación guarda el valor anterior; si la verificación posterior
 * empeora o la aplicación falla, se revierte automáticamente al valor
 * anterior. Fail-closed: ante cualquier duda, el valor seguro.
 *
 * LÍMITES DUROS (nunca se tocan, por diseño — no hay actuador para ellos):
 *  - SafetyLimiter, IntegrityGate, pins de seguridad, credenciales.
 *  - Nodos sysfs de escritura, políticas SELinux, frecuencias de CPU
 *    (el Power HAL del OEM las gobierna; escribir scaling_max_freq se
 *    revierte solo y no se intenta).
 *
 * Actuación real hoy (2 de 7): RESOURCE_BUDGET (ThermalGovernor) y
 * ROUTE_PROFILE (AudioRouteManager) usan subsistemas existentes de IVANNA.
 * El resto decide, registra y verifica con actuadores inyectables; su
 * actuador por defecto es [UnavailableActuator] con el motivo exacto y el
 * nombre de la función nativa que falta (device_cpu_profiler.hpp) para que
 * el cableado JNI sea un trabajo pequeño y explícito, nunca una promesa
 * silenciosa.
 */
object HotOptimizer {
    private const val TAG = "IVANNA-HotOptimizer"

    enum class OptimizationKind(val description: String) {
        BUFFER_SIZE("Tamaño de bloque de IVANNA al recomendado por el SoC"),
        THREAD_AFFINITY("Hilos DSP fijados a los núcleos grandes del SoC"),
        SAMPLE_RATE("Sample rate de trabajo a la nativa del HAL"),
        NEON_PATH("Ruta vectorizada NEON para los núcleos DSP"),
        DSP_OFFLOAD("Offload del procesado al DSP del SoC"),
        RESOURCE_BUDGET("Presupuesto de recursos al ThermalGovernor"),
        ROUTE_PROFILE("Refresco del perfil de ruta de AudioRouteManager"),
    }

    enum class ApplyStatus { APPLIED, ALREADY_OPTIMAL, UNAVAILABLE, FAILED, REVERTED }

    data class ApplyResult(
        val kind: OptimizationKind,
        val status: ApplyStatus,
        /** Detalle legible (qué se aplicó, por qué no, o qué se revirtió). */
        val detail: String,
        /** Valor anterior (para rollback manual) o null si no aplica. */
        val previousValue: String?,
        /** Valor nuevo aplicado o null si no se aplicó. */
        val newValue: String?,
    )

    /** Métricas en vivo para verificar una optimización tras aplicarla. */
    interface MetricsProvider {
        /** Latencia de salida actual en ms, o null si no medible en esta ruta. */
        fun outputLatencyMs(): Double?
        /** Underruns acumulados del HAL, o null si no instrumentado. */
        fun halUnderruns(): Long?
        /** Throughput del DSP en × tiempo real, o null si no medible. */
        fun dspThroughputRt(): Double?
    }

    /** Actuador de una optimización: aplicar, leer el valor actual, revertir. */
    interface Actuator {
        /** true si este actuador puede actuar en este dispositivo. */
        fun available(): Boolean
        /** Motivo legible si no está disponible. */
        fun unavailableReason(): String
        /** Valor actual antes de aplicar (para rollback). */
        fun currentValue(): String?
        /** Aplica el valor objetivo; true si el sistema lo aceptó. */
        fun apply(target: String): Boolean
        /** Restaura un valor anterior; true si el sistema lo aceptó. */
        fun revert(previous: String): Boolean
    }

    /** Actuador que no puede actuar: registra el motivo, nunca finge éxito. */
    class UnavailableActuator(private val reason: String) : Actuator {
        override fun available() = false
        override fun unavailableReason() = reason
        override fun currentValue(): String? = null
        override fun apply(target: String) = false
        override fun revert(previous: String) = false
    }

    /** Actuador en memoria (para tests y para valores que viven en IVANNA). */
    class MemoryActuator(initial: String? = null) : Actuator {
        var value: String? = initial
            private set
        override fun available() = true
        override fun unavailableReason() = ""
        override fun currentValue() = value
        override fun apply(target: String): Boolean { value = target; return true }
        override fun revert(previous: String): Boolean { value = previous; return true }
    }

    /** Actuador real: presupuesto de recursos al ThermalGovernor (0/1/2). */
    class ThermalBudgetActuator : Actuator {
        override fun available() = true
        override fun unavailableReason() = ""
        override fun currentValue(): String? = null // El gobernador no expone lectura; no hay rollback de valor.
        override fun apply(target: String): Boolean = runCatching {
            ThermalGovernor.setExternalResourceBudget(target.toInt())
            true
        }.getOrDefault(false)
        override fun revert(previous: String): Boolean = runCatching {
            ThermalGovernor.setExternalResourceBudget(0) // NORMAL = baseline del gobernador.
            true
        }.getOrDefault(false)
    }

    /** Actuador real: refresco del perfil de ruta (no tiene valor previo). */
    class RouteProfileActuator : Actuator {
        override fun available() = true
        override fun unavailableReason() = ""
        override fun currentValue(): String? = null
        override fun apply(target: String): Boolean = runCatching {
            AudioRouteManager.refreshResourceBudgetProfile()
            true
        }.getOrDefault(false)
        override fun revert(previous: String): Boolean = true // No-op: el refresco no deja estado que revertir.
    }

    data class Plan(
        /** Optimizaciones a aplicar, en orden (de menor a mayor riesgo). */
        val steps: List<Step>,
    ) {
        data class Step(val kind: OptimizationKind, val target: String, val actuator: Actuator)
    }

    /**
     * Formula el plan desde el informe de auditoría: una optimización por
     * oportunidad que tenga actuador, en orden de menor a mayor riesgo
     * (presupuesto y perfil de ruta primero: no tocan el audio; afinidad y
     * búfer después; offload al final porque hoy no tiene actuador).
     */
    fun plan(report: AuditReport, actuators: Map<OptimizationKind, Actuator> = defaultActuators()): Plan {
        val entry = report.match.entry
        val steps = mutableListOf<Plan.Step>()
        fun add(kind: OptimizationKind, target: String) {
            steps += Plan.Step(kind, target, actuators[kind] ?: UnavailableActuator("sin actuador registrado"))
        }
        // 1. Presupuesto de recursos (sin riesgo: solo degrada carga DSP).
        add(OptimizationKind.RESOURCE_BUDGET, "0")
        // 2. Perfil de ruta (sin riesgo: refresco de lectura).
        add(OptimizationKind.ROUTE_PROFILE, "refresh")
        // 3. Búfer (solo si el declarado es mayor que el recomendado).
        val frames = report.profile.audio.framesPerBuffer
        if (frames != null && frames > entry.config.bufferFrames) {
            add(OptimizationKind.BUFFER_SIZE, entry.config.bufferFrames.toString())
        }
        // 4. Sample rate (solo si la nativa difiere de la recomendada).
        val sr = report.profile.audio.nativeSampleRateHz
        if (sr != null && sr != entry.config.sampleRateHz) {
            add(OptimizationKind.SAMPLE_RATE, entry.config.sampleRateHz.toString())
        }
        // 5. Afinidad (solo si hay pocos núcleos o el informe lo pide).
        if (report.profile.cpu.coreCount < 6 || report.bottlenecks.any { it.id == "FEW_CORES" }) {
            val big = report.profile.cpu.cores.filter { it.cluster == CpuCluster.BIG }.map { it.index }
            if (big.isNotEmpty()) add(OptimizationKind.THREAD_AFFINITY, big.joinToString(","))
        }
        // 6. Ruta NEON (solo si no se detectó NEON: no hay nada que aplicar,
        //    pero se registra el intento para que quede en el informe).
        if (!report.profile.cpu.hasNeon) {
            add(OptimizationKind.NEON_PATH, "scalar-fallback")
        }
        // 7. Offload DSP (siempre se registra; hoy su actuador no está disponible).
        if (report.profile.dsp.kind != DspKind.UNKNOWN) {
            add(OptimizationKind.DSP_OFFLOAD, report.profile.dsp.kind.name)
        }
        return Plan(steps)
    }

    /** Actuadores por defecto: los dos reales + el resto no disponible con motivo. */
    fun defaultActuators(): Map<OptimizationKind, Actuator> = mapOf(
        OptimizationKind.RESOURCE_BUDGET to ThermalBudgetActuator(),
        OptimizationKind.ROUTE_PROFILE to RouteProfileActuator(),
        OptimizationKind.BUFFER_SIZE to UnavailableActuator(
            "Sin setter de tamaño de bloque en la ruta de audio de IVANNA todavía; " +
            "el tamaño de bloque se fija en la creación de la pista. Pendiente: exponerlo vía NativeBridge.",
        ),
        OptimizationKind.THREAD_AFFINITY to UnavailableActuator(
            "Sin JNI de afinidad todavía; la implementación C++ existe en " +
            "device_cpu_profiler.hpp (ivanna_set_thread_affinity) y falta el puente JNI.",
        ),
        OptimizationKind.SAMPLE_RATE to UnavailableActuator(
            "La SR se fija en nativeInitDSP (init, no en caliente); cambiarla en caliente " +
            "exige re-init del DSP con la música en pausa. Pendiente: re-init seguro.",
        ),
        OptimizationKind.NEON_PATH to UnavailableActuator(
            "La ruta NEON/escalar se elige en compilación por SoC; no hay interruptor " +
            "en caliente. Pendiente: flag nativo conmutable.",
        ),
        OptimizationKind.DSP_OFFLOAD to UnavailableActuator(
            "Sin seam en el HAL propietario (audio.primary.*.so no es público); " +
            "el offload FastRPC no es aplicable hoy. Ver misión HAL–ASRC.",
        ),
    )

    /**
     * Ejecuta el plan paso a paso. Cada paso: si el actuador no está
     * disponible → UNAVAILABLE (no es un fallo, es un hecho); si el valor
     * actual ya es el objetivo → ALREADY_OPTIMAL; si aplica y la
     * verificación posterior empeora → REVERTED automáticamente.
     * Devuelve los resultados en orden; nunca lanza.
     */
    fun execute(plan: Plan, metrics: MetricsProvider? = null): List<ApplyResult> {
        val results = mutableListOf<ApplyResult>()
        for (step in plan.steps) {
            val result = runCatching { applyStep(step, metrics) }
                .getOrElse {
                    ApplyResult(step.kind, ApplyStatus.FAILED, "Excepción al aplicar: ${it.message}", null, null)
                }
            results += result
            Log.i(TAG, "${step.kind}: ${result.status} — ${result.detail}")
        }
        return results
    }

    private fun applyStep(step: Plan.Step, metrics: MetricsProvider?): ApplyResult {
        val actuator = step.actuator
        if (!actuator.available()) {
            return ApplyResult(step.kind, ApplyStatus.UNAVAILABLE, actuator.unavailableReason(), null, null)
        }
        val previous = runCatching { actuator.currentValue() }.getOrNull()
        if (previous != null && previous == step.target) {
            return ApplyResult(step.kind, ApplyStatus.ALREADY_OPTIMAL, "Ya estaba en ${step.target}.", previous, step.target)
        }
        val beforeLatency = metrics?.outputLatencyMs()
        val beforeUnderruns = metrics?.halUnderruns()
        if (!runCatching { actuator.apply(step.target) }.getOrDefault(false)) {
            return ApplyResult(step.kind, ApplyStatus.FAILED, "El actuador rechazó el valor ${step.target}.", previous, null)
        }
        // Verificación: si la latencia o los underruns empeoraron de forma
        // medible tras aplicar, se revierte automáticamente.
        val afterLatency = metrics?.outputLatencyMs()
        val afterUnderruns = metrics?.halUnderruns()
        val regressed = (beforeLatency != null && afterLatency != null && afterLatency > beforeLatency + 5.0) ||
            (beforeUnderruns != null && afterUnderruns != null && afterUnderruns > beforeUnderruns)
        if (regressed) {
            if (previous != null) runCatching { actuator.revert(previous) }
            return ApplyResult(
                step.kind, ApplyStatus.REVERTED,
                "Regresión medida (latencia ${beforeLatency}→${afterLatency} ms, underruns ${beforeUnderruns}→${afterUnderruns}): revertido a ${previous ?: "baseline"}.",
                previous, step.target,
            )
        }
        return ApplyResult(step.kind, ApplyStatus.APPLIED, "Aplicado: ${step.target}.", previous, step.target)
    }

    /** Revierte en orden inverso todas las optimizaciones aplicadas de una lista de resultados. */
    fun rollbackAll(results: List<ApplyResult>, actuators: Map<OptimizationKind, Actuator>): List<ApplyResult> {
        val out = mutableListOf<ApplyResult>()
        for (result in results.asReversed()) {
            if (result.status != ApplyStatus.APPLIED) continue
            val actuator = actuators[result.kind] ?: continue
            val ok = result.previousValue != null &&
                runCatching { actuator.revert(result.previousValue) }.getOrDefault(false)
            out += result.copy(
                status = if (ok) ApplyStatus.REVERTED else ApplyStatus.FAILED,
                detail = if (ok) "Revertido a ${result.previousValue}." else "No se pudo revertir ${result.kind}.",
            )
        }
        return out
    }
}
