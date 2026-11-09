package com.ivanna.omega.device

import com.ivanna.omega.safmus.SafmusUniversalKernel.EvidenceKind
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

class HardwareKnowledgeBaseTest {

    @Test fun baseHasMeaningfulCoverage() {
        // 22 SoCs conocidos + 1 genérico; todos con fuente declarada.
        assertTrue("La base debe cubrir ≥20 SoCs", HardwareKnowledgeBase.ENTRIES.size >= 20)
        assertTrue(HardwareKnowledgeBase.ENTRIES.all { it.source != EvidenceKind.UNKNOWN })
        assertTrue(HardwareKnowledgeBase.ENTRIES.all { it.clusters.bigCores + it.clusters.midCores + it.clusters.littleCores >= 4 })
        assertTrue(HardwareKnowledgeBase.ENTRIES.all { it.config.bufferFrames in 128..1024 })
        assertTrue(HardwareKnowledgeBase.ENTRIES.all { it.config.sampleRateHz == 48_000 })
    }

    @Test fun g85EntryMatchesMeasuredData() {
        val entry = HardwareKnowledgeBase.bySocModel("SM6375-AC")
        assertNotNull(entry)
        entry!!
        assertEquals("Snapdragon 6s Gen 3", entry.marketingName)
        assertEquals(2, entry.clusters.bigCores)
        assertEquals(2300, entry.clusters.bigFreqMhz)
        assertEquals(6, entry.clusters.littleCores)
        assertEquals(2000, entry.clusters.littleFreqMhz)
        assertEquals(DspKind.HEXAGON, entry.dsp.kind)
        assertEquals(EvidenceKind.MEASURED, entry.source)
        assertTrue(entry.note.contains("47999.7"))
        assertTrue(entry.note.contains("blair"))
    }

    @Test fun lookupExactSocForG85() {
        val match = HardwareKnowledgeBase.lookup(TestProfiles.g85Profile())
        assertEquals(HardwareKnowledgeBase.MatchKind.EXACT_SOC, match.kind)
        assertEquals("SM6375-AC", match.entry.socModel)
    }

    @Test fun lookupByCodenameWhenSocModelMissing() {
        val profile = TestProfiles.g85Profile().copy(
            cpu = TestProfiles.g85Profile().cpu.copy(socModel = null),
        )
        val match = HardwareKnowledgeBase.lookup(profile)
        assertEquals(HardwareKnowledgeBase.MatchKind.CODENAME, match.kind)
        assertEquals("SM6375-AC", match.entry.socModel)
    }

    @Test fun lookupFamilyFallbackForUnknownSnapdragon8() {
        val profile = TestProfiles.g85Profile().copy(
            cpu = TestProfiles.g85Profile().cpu.copy(socModel = "SM9999", hardware = "Qualcomm SM9999"),
            android = TestProfiles.g85Profile().android.copy(board = "unknown", hardware = "unknown"),
        )
        val match = HardwareKnowledgeBase.lookup(profile)
        assertEquals(HardwareKnowledgeBase.MatchKind.FAMILY, match.kind)
        assertEquals("SM8650", match.entry.socModel) // Genérico de la familia 8.
    }

    @Test fun lookupGenericForFullyUnknownSoc() {
        val match = HardwareKnowledgeBase.lookup(TestProfiles.unknownSocProfile())
        assertEquals(HardwareKnowledgeBase.MatchKind.GENERIC, match.kind)
        assertEquals("GENERIC-ARM64", match.entry.socModel)
        assertEquals(512, match.entry.config.bufferFrames) // Conservador.
    }

    @Test fun tensorG2AndG3DisambiguatedByCodename() {
        val g2 = TestProfiles.g85Profile().copy(
            cpu = TestProfiles.g85Profile().cpu.copy(socModel = "GS201"),
            android = TestProfiles.g85Profile().android.copy(board = "cloudripper", hardware = "cloudripper"),
        )
        val g3 = TestProfiles.g85Profile().copy(
            cpu = TestProfiles.g85Profile().cpu.copy(socModel = "GS201"),
            android = TestProfiles.g85Profile().android.copy(board = "zuma", hardware = "zuma"),
        )
        assertEquals("Tensor G2", HardwareKnowledgeBase.lookup(g2).entry.marketingName)
        assertEquals("Tensor G3", HardwareKnowledgeBase.lookup(g3).entry.marketingName)
    }

    @Test fun recommendedThreadsNeverExceedBigPlusMidCores() {
        for (entry in HardwareKnowledgeBase.ENTRIES) {
            val perfCores = entry.clusters.bigCores + entry.clusters.midCores
            assertTrue(
                "${entry.socModel}: ${entry.config.audioThreads} hilos > $perfCores núcleos de rendimiento",
                entry.config.audioThreads <= perfCores,
            )
        }
    }
}
