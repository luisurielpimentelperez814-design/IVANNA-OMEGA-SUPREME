package com.ivanna.omega.device

/**
 * Perfiles sintéticos para los tests JVM del paquete device.
 * Todos los datos son de prueba; el perfil del G85 real usa los valores
 * medidos en vivo que documenta HardwareKnowledgeBase (SM6375-AC).
 */
object TestProfiles {

    fun g85Profile(
        framesPerBuffer: Int? = 512,
        nativeSampleRateHz: Int? = 48_000,
        coreCount: Int = 8,
        hasNeon: Boolean = true,
        totalRamMb: Long? = 8192,
        hasLowLatency: Boolean = true,
    ): DeviceProfile {
        val cores = (0 until coreCount).map { i ->
            CpuCoreInfo(
                index = i, modelName = "ARMv8 Processor rev 4 (v8l)",
                implementer = 0x41, part = if (i < 2) 0xd41 else 0xd05,
                maxFreqKhz = if (i < 2) 2_300_000 else 2_000_000,
                cluster = if (i < 2) CpuCluster.BIG else CpuCluster.LITTLE,
            )
        }
        return DeviceProfile(
            cpu = CpuInfo(
                coreCount = coreCount, onlineCount = coreCount, cores = cores,
                hasNeon = hasNeon, hasNeonFp16 = true, abi = "arm64-v8a",
                hardware = "Qualcomm Technologies, Inc SM6375-AC", socModel = "SM6375-AC",
            ),
            gpu = GpuInfo(renderer = "Adreno 619", glEsVersion = "3.2", hasVulkan = true),
            dsp = DspInfo(DspKind.HEXAGON, "Hexagon 692", offloadAvailable = false),
            audio = AudioSubsystem(
                halName = "audio.primary.blair.so", nativeSampleRateHz = nativeSampleRateHz,
                framesPerBuffer = framesPerBuffer, supportedRatesHz = listOf(44_100, 48_000),
                supportedEncodings = listOf("PCM_16BIT", "PCM_FLOAT"), outputChannels = 2,
                hasLowLatencyFeature = hasLowLatency, hasProAudioFeature = true,
                hasBuiltinSpeaker = true, hasBuiltinMic = true, micCount = 2,
                hasUsbAudioOutput = true, hasBluetoothA2dpOutput = true,
                measuredOutputLatencyMs = 21.0,
            ),
            android = AndroidInfo(
                version = "15", apiLevel = 35, manufacturer = "motorola", model = "Moto G85 5G",
                board = "blair", hardware = "blair", fingerprint = "motorola/test",
                selinuxEnforcing = true, isRoot = true, magiskPresent = true,
                magiskModules = listOf("ivanna_omega_supreme"), totalRamMb = totalRamMb,
            ),
            collectedAtMs = 1_790_000_000_000L,
        )
    }

    fun unknownSocProfile(): DeviceProfile = g85Profile().copy(
        cpu = g85Profile().cpu.copy(socModel = "XX9999", hardware = "Unknown SoC"),
        android = g85Profile().android.copy(board = "unknownboard", hardware = "unknownboard"),
    )
}
