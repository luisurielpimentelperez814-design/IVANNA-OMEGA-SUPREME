package com.ivanna.omega.device

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class DeviceProfileParserTest {

    private val bigLittleCpuinfo = """
        processor	: 0
        model name	: ARMv8 Processor rev 4 (v8l)
        CPU implementer	: 0x41
        CPU part	: 0xd05

        processor	: 1
        model name	: ARMv8 Processor rev 4 (v8l)
        CPU implementer	: 0x41
        CPU part	: 0xd05

        processor	: 2
        model name	: ARMv8 Processor rev 4 (v8l)
        CPU implementer	: 0x41
        CPU part	: 0xd41

        processor	: 3
        model name	: ARMv8 Processor rev 4 (v8l)
        CPU implementer	: 0x41
        CPU part	: 0xd41

        Hardware	: Qualcomm Technologies, Inc SM6375-AC
    """.trimIndent()

    @Test fun parsesBigLittleCpuinfo() {
        val cores = DeviceProfiler.parseCpuinfo(bigLittleCpuinfo)
        assertEquals(4, cores.size)
        assertEquals(0, cores[0].index)
        assertEquals(3, cores[3].index)
        assertEquals(0x41, cores[0].implementer)
        assertEquals(0xd05, cores[0].part) // Cortex-A55
        assertEquals(0xd41, cores[2].part) // Cortex-A78
        assertEquals("ARMv8 Processor rev 4 (v8l)", cores[2].modelName)
    }

    @Test fun emptyOrGarbageCpuinfoYieldsEmptyList() {
        assertTrue(DeviceProfiler.parseCpuinfo("").isEmpty())
        assertTrue(DeviceProfiler.parseCpuinfo("Hardware\t: Foo\n").isEmpty())
        assertTrue(DeviceProfiler.parseCpuinfo("   \n\n").isEmpty())
    }

    @Test fun hardwareFieldIsExtracted() {
        assertEquals(
            "Qualcomm Technologies, Inc SM6375-AC",
            DeviceProfiler.parseCpuinfoField(bigLittleCpuinfo, "Hardware"),
        )
        assertNull(DeviceProfiler.parseCpuinfoField(bigLittleCpuinfo, "NoExiste"))
    }

    @Test fun cpuFreqParsing() {
        assertEquals(2_300_000L, DeviceProfiler.parseCpuFreqKhz("2300000\n"))
        assertEquals(2_000_000L, DeviceProfiler.parseCpuFreqKhz("  2000000  "))
        assertNull(DeviceProfiler.parseCpuFreqKhz("no-numérico"))
        assertNull(DeviceProfiler.parseCpuFreqKhz(""))
    }

    @Test fun clusterClassificationByRelativeFrequency() {
        val cores = (0 until 8).map { i ->
            CpuCoreInfo(i, null, 0x41, 0xd41, if (i < 2) 2_300_000 else 2_000_000, CpuCluster.UNKNOWN)
        }
        val classified = DeviceProfiler.classifyClusters(cores)
        assertEquals(CpuCluster.BIG, classified[0].cluster)
        assertEquals(CpuCluster.BIG, classified[1].cluster)
        assertEquals(CpuCluster.LITTLE, classified[2].cluster)
        assertEquals(CpuCluster.LITTLE, classified[7].cluster)
    }

    @Test fun clusterClassificationWithoutFrequenciesStaysUnknown() {
        val cores = (0 until 4).map { i ->
            CpuCoreInfo(i, null, 0x41, 0xd41, null, CpuCluster.UNKNOWN)
        }
        val classified = DeviceProfiler.classifyClusters(cores)
        assertTrue(classified.all { it.cluster == CpuCluster.UNKNOWN })
    }

    @Test fun fingerprintIsStableAndIgnoresVolatileFields() {
        val a = TestProfiles.g85Profile()
        val b = TestProfiles.g85Profile().copy(collectedAtMs = 9_999_999L)
        assertEquals(a.fingerprint(), b.fingerprint())
        assertEquals(16, a.fingerprint().length)
        // Otro modelo → otra huella.
        val c = TestProfiles.g85Profile().copy(
            android = TestProfiles.g85Profile().android.copy(model = "Moto G84 5G"),
        )
        assertTrue(a.fingerprint() != c.fingerprint())
    }

    @Test fun profileDescribesItselfWithoutNullsLeaking() {
        val lines = TestProfiles.g85Profile().describe()
        assertEquals(5, lines.size)
        assertTrue(lines.joinToString(" ").contains("Moto G85 5G"))
        assertTrue(lines.joinToString(" ").contains("SM6375-AC"))
    }

    @Test fun jsonRoundTripKeepsStableFields() {
        val json = TestProfiles.g85Profile().toJson()
        assertTrue(json.contains("\"socModel\":\"SM6375-AC\""))
        assertTrue(json.contains("\"halName\":\"audio.primary.blair.so\""))
        assertTrue(json.contains("\"coreCount\":8"))
        assertTrue(json.contains("\"isRoot\":true"))
    }
}
