package com.ivanna.omega.device

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class HotOptimizerTest {

    private fun memoryActuators(): Map<HotOptimizer.OptimizationKind, HotOptimizer.Actuator> =
        HotOptimizer.OptimizationKind.values().associateWith { HotOptimizer.MemoryActuator("unset") }

    private class SequencedMetrics(
        private val latencies: List<Double?>,
        private val underruns: List<Long?>,
    ) : HotOptimizer.MetricsProvider {
        private var latencyCalls = 0
        private var underrunCalls = 0
        override fun outputLatencyMs(): Double? = latencies.getOrNull(latencyCalls++)
        override fun halUnderruns(): Long? = underruns.getOrNull(underrunCalls++)
        override fun dspThroughputRt(): Double? = null
    }

    @Test fun planOrdersSafestFirstAndIncludesBufferWhenOversized() {
        val report = DeviceAuditor.audit(TestProfiles.g85Profile(framesPerBuffer = 512))
        val plan = HotOptimizer.plan(report, memoryActuators())
        val kinds = plan.steps.map { it.kind }
        assertEquals(HotOptimizer.OptimizationKind.RESOURCE_BUDGET, kinds.first())
        assertEquals(HotOptimizer.OptimizationKind.ROUTE_PROFILE, kinds[1])
        assertTrue(kinds.contains(HotOptimizer.OptimizationKind.BUFFER_SIZE))
        val buffer = plan.steps.first { it.kind == HotOptimizer.OptimizationKind.BUFFER_SIZE }
        assertEquals("256", buffer.target) // Recomendado del G85.
    }

    @Test fun planOmitsBufferWhenAlreadyOptimal() {
        val report = DeviceAuditor.audit(TestProfiles.g85Profile(framesPerBuffer = 256))
        val plan = HotOptimizer.plan(report, memoryActuators())
        assertTrue(plan.steps.none { it.kind == HotOptimizer.OptimizationKind.BUFFER_SIZE })
    }

    @Test fun planAlwaysIncludesDspOffloadAsRecordedOpportunity() {
        val report = DeviceAuditor.audit(TestProfiles.g85Profile())
        val plan = HotOptimizer.plan(report, memoryActuators())
        assertTrue(plan.steps.any { it.kind == HotOptimizer.OptimizationKind.DSP_OFFLOAD })
    }

    @Test fun applySucceedsAndSecondApplyIsAlreadyOptimal() {
        val actuator = HotOptimizer.MemoryActuator("256")
        val plan = HotOptimizer.Plan(
            listOf(HotOptimizer.Plan.Step(HotOptimizer.OptimizationKind.BUFFER_SIZE, "256", actuator)),
        )
        val first = HotOptimizer.execute(plan)
        assertEquals(HotOptimizer.ApplyStatus.APPLIED, first[0].status)
        assertEquals("256", actuator.value)
        val second = HotOptimizer.execute(plan)
        assertEquals(HotOptimizer.ApplyStatus.ALREADY_OPTIMAL, second[0].status)
    }

    @Test fun unavailableActuatorIsReportedNotFaked() {
        val actuator = HotOptimizer.UnavailableActuator("sin seam en el HAL propietario")
        val plan = HotOptimizer.Plan(
            listOf(HotOptimizer.Plan.Step(HotOptimizer.OptimizationKind.DSP_OFFLOAD, "HEXAGON", actuator)),
        )
        val results = HotOptimizer.execute(plan)
        assertEquals(HotOptimizer.ApplyStatus.UNAVAILABLE, results[0].status)
        assertTrue(results[0].detail.contains("HAL propietario"))
    }

    @Test fun measuredRegressionAutoReverts() {
        val actuator = HotOptimizer.MemoryActuator("512")
        val plan = HotOptimizer.Plan(
            listOf(HotOptimizer.Plan.Step(HotOptimizer.OptimizationKind.BUFFER_SIZE, "256", actuator)),
        )
        // Latencia antes 20 ms → después 30 ms: regresión > 5 ms → revertir.
        val metrics = SequencedMetrics(listOf(20.0, 30.0), listOf(100L, 100L))
        val results = HotOptimizer.execute(plan, metrics)
        assertEquals(HotOptimizer.ApplyStatus.REVERTED, results[0].status)
        assertEquals("512", actuator.value) // De vuelta al valor anterior.
        assertTrue(results[0].detail.contains("Regresión medida"))
    }

    @Test fun noRegressionKeepsAppliedValue() {
        val actuator = HotOptimizer.MemoryActuator("512")
        val plan = HotOptimizer.Plan(
            listOf(HotOptimizer.Plan.Step(HotOptimizer.OptimizationKind.BUFFER_SIZE, "256", actuator)),
        )
        val metrics = SequencedMetrics(listOf(20.0, 19.5), listOf(100L, 100L))
        val results = HotOptimizer.execute(plan, metrics)
        assertEquals(HotOptimizer.ApplyStatus.APPLIED, results[0].status)
        assertEquals("256", actuator.value)
    }

    @Test fun underrunRegressionAlsoReverts() {
        val actuator = HotOptimizer.MemoryActuator("512")
        val plan = HotOptimizer.Plan(
            listOf(HotOptimizer.Plan.Step(HotOptimizer.OptimizationKind.BUFFER_SIZE, "256", actuator)),
        )
        val metrics = SequencedMetrics(listOf(20.0, 20.0), listOf(100L, 101L))
        val results = HotOptimizer.execute(plan, metrics)
        assertEquals(HotOptimizer.ApplyStatus.REVERTED, results[0].status)
        assertEquals("512", actuator.value)
    }

    @Test fun failingActuatorReportsFailedWithoutCrashing() {
        val failing = object : HotOptimizer.Actuator {
            override fun available() = true
            override fun unavailableReason() = ""
            override fun currentValue(): String? = "512"
            override fun apply(target: String) = false
            override fun revert(previous: String) = true
        }
        val plan = HotOptimizer.Plan(
            listOf(HotOptimizer.Plan.Step(HotOptimizer.OptimizationKind.BUFFER_SIZE, "256", failing)),
        )
        val results = HotOptimizer.execute(plan)
        assertEquals(HotOptimizer.ApplyStatus.FAILED, results[0].status)
    }

    @Test fun rollbackAllRestoresInReverseOrder() {
        val a1 = HotOptimizer.MemoryActuator("old-a")
        val a2 = HotOptimizer.MemoryActuator("old-b")
        val actuators = mapOf(
            HotOptimizer.OptimizationKind.BUFFER_SIZE to a1 as HotOptimizer.Actuator,
            HotOptimizer.OptimizationKind.SAMPLE_RATE to a2 as HotOptimizer.Actuator,
        )
        val plan = HotOptimizer.Plan(
            listOf(
                HotOptimizer.Plan.Step(HotOptimizer.OptimizationKind.BUFFER_SIZE, "new-a", a1),
                HotOptimizer.Plan.Step(HotOptimizer.OptimizationKind.SAMPLE_RATE, "new-b", a2),
            ),
        )
        val applied = HotOptimizer.execute(plan)
        assertTrue(applied.all { it.status == HotOptimizer.ApplyStatus.APPLIED })
        val rolledBack = HotOptimizer.rollbackAll(applied, actuators)
        assertEquals(2, rolledBack.size)
        assertTrue(rolledBack.all { it.status == HotOptimizer.ApplyStatus.REVERTED })
        assertEquals("old-a", a1.value)
        assertEquals("old-b", a2.value)
    }

    @Test fun defaultActuatorsMarkFiveAsUnavailableWithReasons() {
        val defaults = HotOptimizer.defaultActuators()
        val unavailable = defaults.filter { !it.value.available() }
        assertEquals(5, unavailable.size)
        assertTrue(unavailable.values.all { it.unavailableReason().isNotBlank() })
        // Los dos reales sí están disponibles (sin afirmar que actúen en JVM).
        assertTrue(defaults[HotOptimizer.OptimizationKind.RESOURCE_BUDGET]!!.available())
        assertTrue(defaults[HotOptimizer.OptimizationKind.ROUTE_PROFILE]!!.available())
    }
}
