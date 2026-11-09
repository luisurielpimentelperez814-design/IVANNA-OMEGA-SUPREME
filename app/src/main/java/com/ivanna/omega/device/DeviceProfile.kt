package com.ivanna.omega.device

import java.security.MessageDigest

/**
 * Modelo de datos del perfil de dispositivo de IVANNA.
 *
 * Puro Kotlin (sin dependencias de Android): las clases de este archivo se
 * pueden construir y probar en JVM. El [DeviceProfiler] es quien las rellena
 * desde APIs de Android y lecturas de /proc y /sys; todo lo que no se pudo
 * determinar queda en `null` / "no detectado" en lugar de inventarse.
 *
 * Regla de oro: un campo `null` significa "no medido", nunca "cero".
 * Ningún consumidor debe tratar `null` como 0.
 */
enum class CpuCluster { BIG, MID, LITTLE, UNKNOWN }

data class CpuCoreInfo(
    /** Índice del núcleo según /proc/cpuinfo (0..n-1). */
    val index: Int,
    /** Nombre del modelo si /proc/cpuinfo lo declara (p. ej. "ARMv8 Processor rev 4"). */
    val modelName: String?,
    /** CPU implementer (MIDR) si se pudo leer, p. ej. 0x41 = ARM. */
    val implementer: Int?,
    /** CPU part (MIDR) si se pudo leer, p. ej. 0xd05 = Cortex-A55. */
    val part: Int?,
    /** Frecuencia máxima declarada por el kernel en kHz, o null si /sys no la expone. */
    val maxFreqKhz: Long?,
    /** Clúster estimado por frecuencia máxima relativa dentro del SoC. */
    val cluster: CpuCluster,
)

enum class DspKind { HEXAGON, MTK_APU, EXYNOS_NPU, TENSOR_TPU, KIRIN_NPU, MALI_GPU_COMPUTE, UNKNOWN }

data class CpuInfo(
    val coreCount: Int,
    /** Núcleos en línea según Runtime.availableProcessors(). */
    val onlineCount: Int,
    val cores: List<CpuCoreInfo>,
    /** NEON/ASIMD disponible (obligatorio en ARMv8-A; si es false, algo va muy mal). */
    val hasNeon: Boolean,
    /** FP16 NEON (ARMv8.2+); null si no se pudo determinar. */
    val hasNeonFp16: Boolean?,
    /** ABI primaria, p. ej. "arm64-v8a". */
    val abi: String,
    /** Campo "Hardware" de /proc/cpuinfo, o null. */
    val hardware: String?,
    /** Modelo de SoC declarado por el sistema (Build.SOC_MODEL, API 31+), o null. */
    val socModel: String?,
)

data class GpuInfo(
    val renderer: String?,
    val glEsVersion: String?,
    val hasVulkan: Boolean,
)

data class DspInfo(
    val kind: DspKind,
    /** Detalle legible, p. ej. "Hexagon 692 (familia SM6375)". */
    val detail: String?,
    /** true si IVANNA tiene hoy un camino de offload usable hacia este DSP. */
    val offloadAvailable: Boolean,
)

data class AudioSubsystem(
    /** Nombre del HAL de audio si se pudo identificar (p. ej. "audio.primary.blair"), o null. */
    val halName: String?,
    /** Sample rate nativo declarado por AudioManager, o null. */
    val nativeSampleRateHz: Int?,
    /** Frames por búfer declarados por AudioManager, o null. */
    val framesPerBuffer: Int?,
    /** Sample rates que acepta AudioTrack.getMinBufferSize (sondeo sin crear pista). */
    val supportedRatesHz: List<Int>,
    /** Formatos aceptados en el sondeo (subconjunto de PCM_16/24/32/FLOAT). */
    val supportedEncodings: List<String>,
    /** Canales de salida declarados (p. ej. 2). */
    val outputChannels: Int?,
    val hasLowLatencyFeature: Boolean,
    val hasProAudioFeature: Boolean,
    val hasBuiltinSpeaker: Boolean,
    val hasBuiltinMic: Boolean,
    val micCount: Int,
    val hasUsbAudioOutput: Boolean,
    val hasBluetoothA2dpOutput: Boolean,
    /**
     * Latencia de salida medida por AudioFlinger en vivo (ms), si IVANNA la
     * midió en este dispositivo; null = no medida. NUNCA un valor estimado.
     */
    val measuredOutputLatencyMs: Double?,
)

data class AndroidInfo(
    val version: String,
    val apiLevel: Int,
    val manufacturer: String,
    val model: String,
    val board: String,
    val hardware: String,
    val fingerprint: String,
    /** true si /sys/fs/selinux/enforce == "1"; null si no se pudo leer. */
    val selinuxEnforcing: Boolean?,
    val isRoot: Boolean,
    val magiskPresent: Boolean,
    /** Módulos Magisk listados en /data/adb/modules (vacío si no legible). */
    val magiskModules: List<String>,
    /** RAM total en MiB, o null si no se pudo determinar. */
    val totalRamMb: Long?,
)

data class DeviceProfile(
    val cpu: CpuInfo,
    val gpu: GpuInfo,
    val dsp: DspInfo,
    val audio: AudioSubsystem,
    val android: AndroidInfo,
    /** Marca de tiempo Unix (ms) de la recolección. */
    val collectedAtMs: Long,
    /** Versión del esquema del perfil (para migraciones futuras). */
    val schemaVersion: Int = 1,
) {
    /**
     * Huella estable del dispositivo: SHA-256 de los campos que no cambian
     * entre arranques (SoC, modelo, placa, ABI, número de núcleos). NO incluye
     * valores volátiles (RAM libre, frecuencias en línea, timestamps).
     * Dos teléfonos idénticos comparten fingerprint; no es un identificador
     * único de instalación (para eso ya existe ANDROID_ID).
     */
    fun fingerprint(): String {
        val stable = listOf(
            android.manufacturer, android.model, android.board, android.hardware,
            cpu.socModel ?: "", cpu.hardware ?: "", cpu.abi,
            cpu.coreCount.toString(), gpu.renderer ?: "",
        ).joinToString("|")
        val digest = MessageDigest.getInstance("SHA-256")
            .digest(stable.toByteArray(Charsets.UTF_8))
        return digest.joinToString("") { "%02x".format(it) }.take(16)
    }

    /** Resumen legible de una línea por sección, para el log y la UI. */
    fun describe(): List<String> = listOf(
        "Dispositivo: ${android.manufacturer} ${android.model} (${android.board}/${android.hardware}) · Android ${android.version} (API ${android.apiLevel}) · fingerprint ${fingerprint()}",
        "SoC: ${cpu.socModel ?: "no declarado"} · CPU: ${cpu.coreCount} núcleos (${cpu.cores.count { it.cluster == CpuCluster.BIG }} grandes / ${cpu.cores.count { it.cluster == CpuCluster.LITTLE }} pequeños) · NEON: ${if (cpu.hasNeon) "sí" else "NO"} · ABI ${cpu.abi}",
        "DSP: ${dsp.kind} ${dsp.detail ?: ""} · offload IVANNA: ${if (dsp.offloadAvailable) "disponible" else "no disponible"}",
        "Audio: HAL ${audio.halName ?: "no identificado"} · SR nativa ${audio.nativeSampleRateHz ?: "s/d"} Hz · ${audio.framesPerBuffer ?: "s/d"} frames/búfer · latencia medida ${audio.measuredOutputLatencyMs?.let { "%.2f ms".format(it) } ?: "no medida"} · baja latencia ${if (audio.hasLowLatencyFeature) "sí" else "no"} · pro audio ${if (audio.hasProAudioFeature) "sí" else "no"}",
        "Sistema: root ${if (android.isRoot) "sí" else "no"} · Magisk ${if (android.magiskPresent) "sí (${android.magiskModules.size} módulos)" else "no"} · SELinux ${android.selinuxEnforcing?.let { if (it) "enforcing" else "permissive" } ?: "s/d"} · RAM ${android.totalRamMb?.let { "$it MiB" } ?: "s/d"}",
    )

    /** Serialización mínima a JSON (sin dependencias) para persistir el perfil. */
    fun toJson(): String = buildString {
        append("{")
        append("\"schema\":").append(schemaVersion).append(",")
        append("\"fingerprint\":\"").append(fingerprint()).append("\",")
        append("\"collectedAtMs\":").append(collectedAtMs).append(",")
        append("\"manufacturer\":").append(jsonStr(android.manufacturer)).append(",")
        append("\"model\":").append(jsonStr(android.model)).append(",")
        append("\"board\":").append(jsonStr(android.board)).append(",")
        append("\"hardware\":").append(jsonStr(android.hardware)).append(",")
        append("\"socModel\":").append(jsonStr(cpu.socModel ?: "")).append(",")
        append("\"androidVersion\":").append(jsonStr(android.version)).append(",")
        append("\"apiLevel\":").append(android.apiLevel).append(",")
        append("\"coreCount\":").append(cpu.coreCount).append(",")
        append("\"hasNeon\":").append(cpu.hasNeon).append(",")
        append("\"nativeSampleRateHz\":").append(audio.nativeSampleRateHz ?: -1).append(",")
        append("\"framesPerBuffer\":").append(audio.framesPerBuffer ?: -1).append(",")
        append("\"halName\":").append(jsonStr(audio.halName ?: "")).append(",")
        append("\"dspKind\":\"").append(dsp.kind.name).append("\",")
        append("\"isRoot\":").append(android.isRoot).append(",")
        append("\"magiskPresent\":").append(android.magiskPresent).append(",")
        append("\"magiskModules\":[").append(android.magiskModules.joinToString(",") { jsonStr(it) }).append("]")
        append("}")
    }

    companion object {
        internal fun jsonStr(s: String): String =
            "\"" + s.replace("\\", "\\\\").replace("\"", "\\\"") + "\""
    }
}
