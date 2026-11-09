package com.ivanna.omega.device

import com.ivanna.omega.safmus.SafmusUniversalKernel.EvidenceKind

/**
 * Motor de auditoría de dispositivo de IVANNA.
 *
 * Compara el [DeviceProfile] medido contra la [HardwareKnowledgeBase] y
 * produce un informe: qué hardware se detectó, si está en la base (o es
 * genérico), cuellos de botella y oportunidades de mejora — cada una con su
 * valor esperado y su fuente (medido o estimado, nunca mezclados).
 *
 * Lógica pura (sin Android): testeable en JVM. No aplica nada: solo
 * diagnostica. Quien aplica es el [HotOptimizer].
 */
enum class Severity { INFO, WARNING, CRITICAL }

data class Bottleneck(
    val id: String,
    val severity: Severity,
    /** Descripción del problema en lenguaje legible. */
    val description: String,
    /** Valor medido que lo evidencia (p. ej. "512 frames/búfer"). */
    val measured: String,
    /** Valor que la base recomienda. */
    val recommended: String,
    /** Mejora esperada si se corrige, con su fuente. */
    val expectedGain: String,
    val gainSource: EvidenceKind,
)

data class Opportunity(
    val id: String,
    /** Optimización del HotOptimizer que la materializa (o null si no hay actuador). */
    val optimizationId: String?,
    val description: String,
    val expectedGain: String,
    val gainSource: EvidenceKind,
)

data class AuditReport(
    val profile: DeviceProfile,
    val match: HardwareKnowledgeBase.Match,
    val bottlenecks: List<Bottleneck>,
    val opportunities: List<Opportunity>,
    /** Resumen de una línea por sección. */
    val summary: List<String>,
) {
    fun describe(): List<String> = summary + bottlenecks.map {
        "[${it.severity}] ${it.id}: ${it.description} (medido: ${it.measured}; recomendado: ${it.recommended}; ganancia esperada: ${it.expectedGain})"
    } + opportunities.map {
        "[oportunidad] ${it.id}: ${it.description} (ganancia esperada: ${it.expectedGain})"
    }
}

object DeviceAuditor {

    fun audit(profile: DeviceProfile): AuditReport {
        val match = HardwareKnowledgeBase.lookup(profile)
        val entry = match.entry
        val bottlenecks = mutableListOf<Bottleneck>()
        val opportunities = mutableListOf<Opportunity>()

        // ── B1: búfer declarado mayor que el recomendado → latencia ──
        val frames = profile.audio.framesPerBuffer
        if (frames != null && frames > entry.config.bufferFrames) {
            val excessMs = (frames - entry.config.bufferFrames) * 1000.0 / entry.config.sampleRateHz
            bottlenecks += Bottleneck(
                id = "BUFFER_OVERSIZED", severity = Severity.WARNING,
                description = "El búfer de salida declarado es mayor que el recomendado para este SoC: cada búfer extra es latencia que se oye.",
                measured = "$frames frames/búfer",
                recommended = "${entry.config.bufferFrames} frames/búfer",
                expectedGain = "−${"%.1f".format(excessMs)} ms de latencia por búfer",
                gainSource = EvidenceKind.ESTIMATED,
            )
            opportunities += Opportunity(
                id = "BUFFER_SIZE", optimizationId = HotOptimizer.OptimizationKind.BUFFER_SIZE.name,
                description = "Ajustar el tamaño de bloque de IVANNA a ${entry.config.bufferFrames} frames.",
                expectedGain = "−${"%.1f".format(excessMs)} ms de latencia por búfer",
                gainSource = EvidenceKind.ESTIMATED,
            )
        }

        // ── B2: sample rate nativa distinta de 48 kHz → resampling ──
        val sr = profile.audio.nativeSampleRateHz
        if (sr != null && sr != entry.config.sampleRateHz) {
            bottlenecks += Bottleneck(
                id = "NON_NATIVE_SR", severity = Severity.WARNING,
                description = "La SR nativa del HAL no es 48 kHz: todo el audio pasa por un resampler antes de sonar.",
                measured = "$sr Hz",
                recommended = "${entry.config.sampleRateHz} Hz",
                expectedGain = "Eliminar el resampling del HAL (coste CPU y fase)",
                gainSource = EvidenceKind.ESTIMATED,
            )
            opportunities += Opportunity(
                id = "SAMPLE_RATE", optimizationId = HotOptimizer.OptimizationKind.SAMPLE_RATE.name,
                description = "Fijar la SR de trabajo de IVANNA a ${entry.config.sampleRateHz} Hz.",
                expectedGain = "Sin resampling en la ruta",
                gainSource = EvidenceKind.ESTIMATED,
            )
        }

        // ── B3: NEON no detectado (en ARMv8-A es obligatorio: si falta, algo va mal) ──
        if (!profile.cpu.hasNeon) {
            bottlenecks += Bottleneck(
                id = "NO_NEON", severity = Severity.CRITICAL,
                description = "NEON no detectado en un SoC ARMv8: la ruta vectorizada no está disponible y el DSP cae a escalar.",
                measured = "NEON: no",
                recommended = "NEON: sí",
                expectedGain = "2–4× throughput en los núcleos DSP (vectorización 128-bit)",
                gainSource = EvidenceKind.ESTIMATED,
            )
        }

        // ── B4: pocos núcleos → DSP y UI compiten ──
        if (profile.cpu.coreCount < 6) {
            bottlenecks += Bottleneck(
                id = "FEW_CORES", severity = Severity.WARNING,
                description = "Pocos núcleos: el DSP y la UI comparten CPU; un pico de UI puede robarle tiempo al audio.",
                measured = "${profile.cpu.coreCount} núcleos",
                recommended = "Aislar el hilo DSP en un núcleo grande (afinidad)",
                expectedGain = "Menos XRuns bajo carga de UI",
                gainSource = EvidenceKind.ESTIMATED,
            )
            opportunities += Opportunity(
                id = "THREAD_AFFINITY", optimizationId = HotOptimizer.OptimizationKind.THREAD_AFFINITY.name,
                description = "Fijar los hilos DSP a los núcleos grandes del SoC.",
                expectedGain = "Menos XRuns bajo carga de UI",
                gainSource = EvidenceKind.ESTIMATED,
            )
        }

        // ── B5: RAM escasa → riesgo de kills del proceso ──
        val ram = profile.android.totalRamMb
        if (ram != null && ram < 4096) {
            bottlenecks += Bottleneck(
                id = "LOW_RAM", severity = Severity.WARNING,
                description = "RAM total baja: Android puede matar el proceso de audio en segundo plano bajo presión de memoria.",
                measured = "$ram MiB",
                recommended = "≥ 4096 MiB para audio en segundo plano estable",
                expectedGain = "Menor riesgo de muerte del proceso de audio",
                gainSource = EvidenceKind.ESTIMATED,
            )
        }

        // ── B6: sin baja latencia declarada → piso de latencia alto ──
        if (!profile.audio.hasLowLatencyFeature) {
            bottlenecks += Bottleneck(
                id = "NO_LOW_LATENCY", severity = Severity.INFO,
                description = "El fabricante no declara la feature de baja latencia: la latencia mínima achievable es mayor.",
                measured = "FEATURE_AUDIO_LOW_LATENCY: no",
                recommended = "FEATURE_AUDIO_LOW_LATENCY: sí",
                expectedGain = "Ninguna por software: es una declaración del OEM",
                gainSource = EvidenceKind.ESTIMATED,
            )
        }

        // ── B7: DSP presente pero sin camino de offload ──
        if (profile.dsp.kind != DspKind.UNKNOWN && !profile.dsp.offloadAvailable) {
            opportunities += Opportunity(
                id = "DSP_OFFLOAD", optimizationId = HotOptimizer.OptimizationKind.DSP_OFFLOAD.name,
                description = "Offload al ${profile.dsp.detail ?: profile.dsp.kind}: hoy no hay seam (el HAL es propietario); la oportunidad queda registrada, no aplicada.",
                expectedGain = "Descargar el DSP de la CPU (sin medir: sin seam no hay dato)",
                gainSource = EvidenceKind.UNKNOWN,
            )
        }

        // ── B8: deriva del reloj HAL (solo Moto G85 medido) ──
        if (match.entry.socModel == "SM6375-AC") {
            opportunities += Opportunity(
                id = "ASRC_DRIFT", optimizationId = null,
                description = "Deriva medida del HAL: AudioOut_D 47999.7 Hz (−6.25 ppm). El lazo ASRC del write-stage la corrige; es trabajo de la misión HAL–ASRC, no de este optimizador.",
                expectedGain = "Deriva ~0 y menos underruns/overruns (medido: 199/523 en 2.29M writes antes del lazo)",
                gainSource = EvidenceKind.MEASURED,
            )
        }

        // ── B9: HAL térmico sin respuesta → gobernador inerte ──
        opportunities += Opportunity(
            id = "THERMAL_GOVERNOR", optimizationId = HotOptimizer.OptimizationKind.RESOURCE_BUDGET.name,
            description = "Publicar el presupuesto de recursos (NORMAL/CONSERVATIVE/SAFE) al ThermalGovernor para que degrade la carga DSP antes de que el kernel haga throttling.",
            expectedGain = "Continuidad de audio en sesiones largas (sin glitches por throttling)",
            gainSource = EvidenceKind.ESTIMATED,
        )
        opportunities += Opportunity(
            id = "ROUTE_PROFILE", optimizationId = HotOptimizer.OptimizationKind.ROUTE_PROFILE.name,
            description = "Refrescar el perfil de ruta de AudioRouteManager con el presupuesto actual.",
            expectedGain = "Perfil de ruta coherente con el estado térmico y de recursos",
            gainSource = EvidenceKind.ESTIMATED,
        )

        val known = when (match.kind) {
            HardwareKnowledgeBase.MatchKind.EXACT_SOC -> "conocido (coincidencia exacta por SoC)"
            HardwareKnowledgeBase.MatchKind.CODENAME -> "conocido (coincidencia por codename de plataforma)"
            HardwareKnowledgeBase.MatchKind.FAMILY -> "genérico de su familia (el SoC exacto no está en la base)"
            HardwareKnowledgeBase.MatchKind.GENERIC -> "desconocido (perfil genérico ARM64 conservador)"
        }
        val summary = listOf(
            "Dispositivo: ${profile.android.manufacturer} ${profile.android.model} · SoC ${match.entry.marketingName} (${match.entry.socModel}) — $known · fuente: ${match.entry.source} / config: ${match.entry.configSource}",
            "${bottlenecks.size} cuellos de botella (${bottlenecks.count { it.severity == Severity.CRITICAL }} críticos, ${bottlenecks.count { it.severity == Severity.WARNING }} advertencias) · Oportunidades: ${opportunities.size}",
            "Latencia esperada con la configuración recomendada: ${entry.config.expectedLatencyMs} ms (fuente: ${entry.configSource})",
        )
        return AuditReport(profile, match, bottlenecks, opportunities, summary)
    }
}
