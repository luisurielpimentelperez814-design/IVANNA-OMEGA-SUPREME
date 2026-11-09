package com.ivanna.omega.device

import android.app.ActivityManager
import android.content.Context
import android.media.AudioDeviceInfo
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioTrack
import android.os.Build
import android.util.Log
import com.ivanna.omega.core.RootAccess
import com.ivanna.omega.hardware.HardwareProbe
import java.io.File

/**
 * Perfilador de dispositivo de IVANNA.
 *
 * En la primera instalación (y bajo demanda) recolecta TODO lo relevante del
 * equipo con APIs públicas y lecturas de solo lectura de /proc y /sys:
 * NO escribe nada, NO pide permisos nuevos, NO ejecuta `su`, NO toca sysfs
 * de escritura ni el HAL. Cada lectura va en `runCatching`: lo que no se
 * puede determinar queda en `null` ("no detectado"), nunca inventado.
 *
 * Las funciones `parseCpuinfo` / `parseCpuFreqKhz` / `classifyClusters` son
 * puras (texto → datos) para que los tests JVM las ejerciten sin Android.
 */
object DeviceProfiler {
    private const val TAG = "IVANNA-DeviceProfiler"

    /** Sample rates que se sondean con AudioTrack.getMinBufferSize (sin crear pista). */
    private val PROBE_RATES = listOf(44_100, 48_000, 96_000, 192_000)

    /** Último perfil recolectado (null hasta el primer sondeo). */
    @Volatile var lastProfile: DeviceProfile? = null
        private set

    fun profile(context: Context): DeviceProfile {
        val app = context.applicationContext
        val cpu = profileCpu(app)
        val gpu = profileGpu(app)
        val dsp = profileDsp(cpu)
        val audio = profileAudio(app)
        val android = profileAndroid(app)
        return DeviceProfile(
            cpu = cpu, gpu = gpu, dsp = dsp, audio = audio, android = android,
            collectedAtMs = System.currentTimeMillis(),
        ).also { lastProfile = it }
    }

    // ── CPU ──────────────────────────────────────────────────────────────────

    private fun profileCpu(context: Context): CpuInfo {
        val cpuinfoText = runCatching { File("/proc/cpuinfo").readText() }.getOrNull()
        val cores = parseCpuinfo(cpuinfoText ?: "")
        val hardware = cpuinfoText?.let { parseCpuinfoField(it, "Hardware") }
        val socModel = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
            runCatching { Build.SOC_MODEL }.getOrNull() else null
        val abi = Build.SUPPORTED_ABIS.firstOrNull() ?: "unknown"
        // NEON es obligatorio en ARMv8-A: si la ABI es arm64, hay NEON por
        // arquitectura. La confirmación por hardware (getauxval AT_HWCAP)
        // vive en el nativo device_cpu_profiler.hpp; aquí no se afirma lo
        // que no se midió: hasNeon=true solo por ABI arm64 (arquitectura),
        // y hasNeonFp16 queda null hasta que el nativo lo confirme.
        val hasNeon = abi.startsWith("arm64") || abi.startsWith("armeabi")
        return CpuInfo(
            coreCount = cores.size.coerceAtLeast(1),
            onlineCount = Runtime.getRuntime().availableProcessors().coerceAtLeast(1),
            cores = classifyClusters(cores),
            hasNeon = hasNeon,
            hasNeonFp16 = null,
            abi = abi,
            hardware = hardware,
            socModel = socModel,
        )
    }

    /**
     * Parsea /proc/cpuinfo en una lista de núcleos. En ARM64 cada bloque
     * "processor : N" trae model name / CPU implementer / CPU part.
     * Texto vacío o sin bloques → lista vacía (el llamador usa fallback).
     */
    fun parseCpuinfo(text: String): List<CpuCoreInfo> {
        if (text.isBlank()) return emptyList()
        val cores = mutableListOf<CpuCoreInfo>()
        var index = -1
        var model: String? = null
        var implementer: Int? = null
        var part: Int? = null
        fun flush() {
            if (index >= 0) {
                cores += CpuCoreInfo(
                    index = index, modelName = model,
                    implementer = implementer, part = part,
                    maxFreqKhz = readCpuMaxFreqKhz(index),
                    cluster = CpuCluster.UNKNOWN,
                )
            }
        }
        for (raw in text.lines()) {
            val line = raw.trim()
            when {
                line.startsWith("processor") -> {
                    flush()
                    index = line.substringAfter(":").trim().toIntOrNull() ?: (cores.size)
                    model = null; implementer = null; part = null
                }
                line.startsWith("model name") -> model = line.substringAfter(":").trim().takeIf { it.isNotEmpty() }
                line.startsWith("CPU implementer") ->
                    implementer = line.substringAfter(":").trim().removePrefix("0x").toIntOrNull(16)
                line.startsWith("CPU part") ->
                    part = line.substringAfter(":").trim().removePrefix("0x").toIntOrNull(16)
            }
        }
        flush()
        return cores
    }

    fun parseCpuinfoField(text: String, field: String): String? =
        text.lines().firstOrNull { it.trim().startsWith(field) }
            ?.substringAfter(":")?.trim()?.takeIf { it.isNotEmpty() }

    /** Lee /sys/devices/system/cpu/cpuN/cpufreq/scaling_max_freq (kHz). Null si no legible. */
    fun readCpuMaxFreqKhz(cpuIndex: Int): Long? = runCatching {
        File("/sys/devices/system/cpu/cpu$cpuIndex/cpufreq/scaling_max_freq")
            .readText().trim().toLongOrNull()
    }.getOrNull()

    /** Parsea el contenido de scaling_max_freq (texto con un número en kHz). */
    fun parseCpuFreqKhz(text: String): Long? = text.trim().toLongOrNull()

    /**
     * Clasifica los núcleos en clústeres por frecuencia máxima relativa:
     * el/los de mayor frecuencia son BIG, el/los de menor son LITTLE, el
     * resto MID. Con un solo valor distinto (o sin frecuencias) todos quedan
     * UNKNOWN — no se inventa una topología que no se midió.
     */
    fun classifyClusters(cores: List<CpuCoreInfo>): List<CpuCoreInfo> {
        val freqs = cores.mapNotNull { it.maxFreqKhz }.distinct().sorted()
        if (freqs.size < 2) return cores
        val max = freqs.last()
        val min = freqs.first()
        return cores.map { core ->
            val cluster = when (core.maxFreqKhz) {
                null -> CpuCluster.UNKNOWN
                max -> CpuCluster.BIG
                min -> CpuCluster.LITTLE
                else -> CpuCluster.MID
            }
            core.copy(cluster = cluster)
        }
    }

    // ── GPU / DSP ────────────────────────────────────────────────────────────

    private fun profileGpu(context: Context): GpuInfo {
        val caps = runCatching { HardwareProbe.probe(context) }.getOrNull()
        return GpuInfo(
            renderer = null, // El renderer GL exige un contexto EGL; no se crea en el sondeo.
            glEsVersion = caps?.glEsVersion,
            hasVulkan = caps?.hasVulkan == true,
        )
    }

    /**
     * El DSP se infiere de la familia del SoC (Build.HARDWARE / SOC_MODEL):
     * Qualcomm → Hexagon, MediaTek → APU, Exynos → NPU, Tensor → TPU,
     * Kirin → NPU Da Vinci. `offloadAvailable=false` siempre en esta
     * versión: IVANNA no tiene hoy un camino de offload (FastRPC) usable
     * sin el seam del HAL; afirmar lo contrario sería mentir. El
     * HotOptimizer lo marca como oportunidad, no como capacidad.
     */
    fun profileDsp(cpu: CpuInfo): DspInfo {
        val key = (cpu.socModel ?: "") + " " + (cpu.hardware ?: "")
        return when {
            key.contains("SM", ignoreCase = true) || cpu.hardware?.contains("Qualcomm", ignoreCase = true) == true ->
                DspInfo(DspKind.HEXAGON, "Hexagon (familia Qualcomm ${cpu.socModel ?: ""})", offloadAvailable = false)
            key.contains("MT", ignoreCase = true) || cpu.hardware?.contains("MediaTek", ignoreCase = true) == true ->
                DspInfo(DspKind.MTK_APU, "MediaTek APU", offloadAvailable = false)
            key.contains("S5E", ignoreCase = true) || cpu.hardware?.contains("Exynos", ignoreCase = true) == true ->
                DspInfo(DspKind.EXYNOS_NPU, "Exynos NPU", offloadAvailable = false)
            key.contains("GS", ignoreCase = true) || cpu.hardware?.contains("Tensor", ignoreCase = true) == true ->
                DspInfo(DspKind.TENSOR_TPU, "Google Tensor TPU", offloadAvailable = false)
            key.contains("Hi36", ignoreCase = true) || cpu.hardware?.contains("Kirin", ignoreCase = true) == true ->
                DspInfo(DspKind.KIRIN_NPU, "HiSilicon Da Vinci NPU", offloadAvailable = false)
            else -> DspInfo(DspKind.UNKNOWN, null, offloadAvailable = false)
        }
    }

    // ── Audio ────────────────────────────────────────────────────────────────

    private fun profileAudio(context: Context): AudioSubsystem {
        val am = runCatching { context.getSystemService(Context.AUDIO_SERVICE) as? AudioManager }.getOrNull()
        val caps = runCatching { HardwareProbe.probe(context) }.getOrNull()
        val outTypes = runCatching {
            am?.getDevices(AudioManager.GET_DEVICES_OUTPUTS)?.map { it.type }?.toSet()
        }.getOrNull().orEmpty()
        val inTypes = runCatching {
            am?.getDevices(AudioManager.GET_DEVICES_INPUTS)?.map { it.type }?.toSet()
        }.getOrNull().orEmpty()
        // Sondeo de rates sin crear pista: getMinBufferSize devuelve
        // ERROR_BAD_VALUE si el rate no se admite.
        val supportedRates = PROBE_RATES.filter { rate ->
            runCatching {
                AudioTrack.getMinBufferSize(rate, AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT) > 0
            }.getOrDefault(false)
        }
        val supportedEncodings = listOf(
            "PCM_16BIT" to AudioFormat.ENCODING_PCM_16BIT,
            "PCM_24BIT" to AudioFormat.ENCODING_PCM_24BIT_PACKED,
            "PCM_32BIT" to AudioFormat.ENCODING_PCM_32BIT,
            "PCM_FLOAT" to AudioFormat.ENCODING_PCM_FLOAT,
        ).filter { (_, enc) ->
            runCatching {
                AudioTrack.getMinBufferSize(48_000, AudioFormat.CHANNEL_OUT_STEREO, enc) > 0
            }.getOrDefault(false)
        }.map { it.first }
        return AudioSubsystem(
            halName = detectHalName(),
            nativeSampleRateHz = caps?.outputSampleRateHz,
            framesPerBuffer = caps?.outputFramesPerBuffer,
            supportedRatesHz = supportedRates,
            supportedEncodings = supportedEncodings,
            outputChannels = if (AudioDeviceInfo.TYPE_BUILTIN_SPEAKER in outTypes) 2 else null,
            hasLowLatencyFeature = caps?.hasLowLatencyFeature == true,
            hasProAudioFeature = caps?.hasProAudioFeature == true,
            hasBuiltinSpeaker = caps?.hasBuiltinSpeaker == true,
            hasBuiltinMic = caps?.hasBuiltinMic == true,
            micCount = inTypes.count { it == AudioDeviceInfo.TYPE_BUILTIN_MIC },
            hasUsbAudioOutput = caps?.hasUsbAudioOutput == true,
            hasBluetoothA2dpOutput = caps?.hasBluetoothA2dpOutput == true,
            measuredOutputLatencyMs = null, // La mide RoomMeasurement en vivo; no se estima aquí.
        )
    }

    /**
     * Nombre del HAL de audio por listado de solo lectura de /vendor/lib64/hw
     * (audio.primary.<plataforma>.so). Si el directorio no es legible
     * (SELinux), devuelve null: "no identificado", no un nombre inventado.
     */
    fun detectHalName(): String? = runCatching {
        val dir = File("/vendor/lib64/hw")
        if (!dir.isDirectory) return null
        dir.listFiles()
            ?.mapNotNull { it.name.takeIf { n -> n.startsWith("audio.primary.") && n.endsWith(".so") } }
            ?.sorted()?.firstOrNull()
    }.getOrNull()

    // ── Android / sistema ────────────────────────────────────────────────────

    private fun profileAndroid(context: Context): AndroidInfo {
        val am = runCatching { context.getSystemService(Context.ACTIVITY_SERVICE) as? ActivityManager }.getOrNull()
        val mem = ActivityManager.MemoryInfo()
        runCatching { am?.getMemoryInfo(mem) }
        val totalRamMb = if (mem.totalMem > 0) mem.totalMem / (1024 * 1024) else null
        val selinux = runCatching {
            File("/sys/fs/selinux/enforce").readText().trim() == "1"
        }.getOrNull()
        val magiskModules = runCatching {
            File("/data/adb/modules").listFiles()
                ?.filter { it.isDirectory }?.map { it.name }?.sorted().orEmpty()
        }.getOrDefault(emptyList())
        val magiskPresent = magiskModules.isNotEmpty() ||
            runCatching { File("/data/adb/magisk").exists() }.getOrDefault(false)
        return AndroidInfo(
            version = Build.VERSION.RELEASE ?: "unknown",
            apiLevel = Build.VERSION.SDK_INT,
            manufacturer = Build.MANUFACTURER ?: "unknown",
            model = Build.MODEL ?: "unknown",
            board = Build.BOARD ?: "unknown",
            hardware = Build.HARDWARE ?: "unknown",
            fingerprint = Build.FINGERPRINT ?: "unknown",
            selinuxEnforcing = selinux,
            isRoot = runCatching { RootAccess.suBinaryVisible() }.getOrDefault(false),
            magiskPresent = magiskPresent,
            magiskModules = magiskModules,
            totalRamMb = totalRamMb,
        )
    }

    /** Log de una línea por sección del perfil (para el arranque y el diagnóstico). */
    fun logProfile(profile: DeviceProfile) {
        profile.describe().forEach { Log.i(TAG, it) }
    }
}
