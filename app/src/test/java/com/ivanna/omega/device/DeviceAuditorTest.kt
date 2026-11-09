package com.ivanna.omega.device

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class DeviceAuditorTest {

    @Test fun g85WithOversizedBufferReportsBottleneckAndOpportunity() {
        val report = DeviceAuditor.audit(TestProfiles.g85Profile(framesPerBuffer = 512))
        assertEquals("SM6375-AC", report.match.entry.socModel)
        val bottleneck = report.bottlenecks.firstOrNull { it.id == "BUFFER_OVERSIZED" }
        assertTrue("Debe detectar el búfer sobredimensionado", bottleneck != null)
        bottleneck!!
        assertEquals(Severity.WARNING, bottleneck.severity)
        assertTrue(bottleneck.measured.contains("512"))
        assertTrue(bottleneck.recommended.contains("256"))
        assertTrue(report.opportunities.any { it.id == "BUFFER_SIZE" })
    }

    @Test fun g85WithOptimalBufferHasNoBufferBottleneck() {
        val report = DeviceAuditor.audit(TestProfiles.g85Profile(framesPerBuffer = 256))
        assertTrue(report.bottlenecks.none { it.id == "BUFFER_OVERSIZED" })
        assertTrue(report.opportunities.none { it.id == "BUFFER_SIZE" })
    }

    @Test fun nonNativeSampleRateIsFlagged() {
        val report = DeviceAuditor.audit(TestProfiles.g85Profile(nativeSampleRateHz = 44_100))
        assertTrue(report.bottlenecks.any { it.id == "NON_NATIVE_SR" })
        assertTrue(report.opportunities.any { it.id == "SAMPLE_RATE" })
    }

    @Test fun missingNeonIsCritical() {
        val report = DeviceAuditor.audit(TestProfiles.g85Profile(hasNeon = false))
        val bottleneck = report.bottlenecks.firstOrNull { it.id == "NO_NEON" }
        assertTrue(bottleneck != null)
        assertEquals(Severity.CRITICAL, bottleneck!!.severity)
    }

    @Test fun fewCoresFlagged() {
        val report = DeviceAuditor.audit(TestProfiles.g85Profile(coreCount = 4))
        assertTrue(report.bottlenecks.any { it.id == "FEW_CORES" })
        assertTrue(report.opportunities.any { it.id == "THREAD_AFFINITY" })
    }

    @Test fun lowRamFlagged() {
        val report = DeviceAuditor.audit(TestProfiles.g85Profile(totalRamMb = 2048))
        assertTrue(report.bottlenecks.any { it.id == "LOW_RAM" })
    }

    @Test fun g85AlwaysCarriesMeasuredAsrcOpportunity() {
        val report = DeviceAuditor.audit(TestProfiles.g85Profile())
        val asrc = report.opportunities.firstOrNull { it.id == "ASRC_DRIFT" }
        assertTrue(asrc != null)
        assertTrue(asrc!!.description.contains("47999.7"))
        // La deriva es trabajo de la misión HAL–ASRC: sin optimización de este motor.
        assertEquals(null, asrc.optimizationId)
    }

    @Test fun unknownSocStillAuditsWithGenericProfile() {
        val report = DeviceAuditor.audit(TestProfiles.unknownSocProfile())
        assertEquals(HardwareKnowledgeBase.MatchKind.GENERIC, report.match.kind)
        assertTrue(report.summary.isNotEmpty())
        assertTrue(report.describe().joinToString(" ").contains("genérico"))
    }

    @Test fun reportSummaryCountsMatchLists() {
        val report = DeviceAuditor.audit(TestProfiles.g85Profile(framesPerBuffer = 512))
        assertTrue(report.summary.joinToString(" ").contains("${report.bottlenecks.size} cuellos de botella"))
        assertTrue(report.summary.joinToString(" ").contains("Oportunidades: ${report.opportunities.size}"))
    }
}
