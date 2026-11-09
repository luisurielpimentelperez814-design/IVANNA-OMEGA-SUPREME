package com.ivanna.omega.device

import com.ivanna.omega.safmus.SafmusUniversalKernel.EvidenceKind

/**
 * Base de conocimiento de hardware de IVANNA.
 *
 * SoCs conocidos con su configuración de audio recomendada. Cada dato lleva
 * su fuente ([EvidenceKind], misma taxonomía que SafmusUniversalKernel):
 *  - MEASURED: medido en vivo en el dispositivo de desarrollo (Moto G85).
 *  - VENDOR_DOC: hoja pública del fabricante (núcleos, frecuencias, DSP).
 *  - ESTIMATED: derivado por regla (p. ej. búfer recomendado por clase de
 *    SoC) o de la familia; marcado como tal, nunca presentado como medido.
 *  - UNKNOWN: sin dato.
 *
 * Si el dispositivo NO está en la base, el sistema NO inventa datos: usa el
 * perfil genérico de su familia de SoC (prefijo del modelo) + la medición en
 * vivo del [DeviceProfiler]. Un SoC desconocido nunca es un error: es un
 * perfil genérico con recomendaciones conservadoras.
 */
data class ClusterSpec(
    /** Núcleos grandes (X/A7x) y su frecuencia máxima en MHz. */
    val bigCores: Int, val bigFreqMhz: Int,
    /** Núcleos medios (A7x) si los hay. */
    val midCores: Int = 0, val midFreqMhz: Int = 0,
    /** Núcleos pequeños (A5x) y su frecuencia máxima en MHz. */
    val littleCores: Int, val littleFreqMhz: Int,
)

data class RecommendedAudioConfig(
    /** Sample rate nativo que el HAL prefiere (48 kHz en Android). */
    val sampleRateHz: Int = 48_000,
    /** Frames por búfer recomendados para la ruta de IVANNA. */
    val bufferFrames: Int,
    /** Hilos de audio/DSP recomendados (nunca más que núcleos grandes+medios). */
    val audioThreads: Int,
    /** Afinidad recomendada para los hilos DSP: núcleos grandes. */
    val dspOnBigCores: Boolean = true,
    /** Ruta NEON (siempre true en ARMv8-A; false solo si el SoC no la tiene). */
    val neonPath: Boolean = true,
    /** Offload al DSP del SoC (Hexagon FastRPC, etc.) — false hasta tener el seam. */
    val dspOffload: Boolean = false,
    /** Latencia de salida esperable (ms) con esta configuración. */
    val expectedLatencyMs: Double,
)

data class SocEntry(
    /** Modelo de SoC según Build.SOC_MODEL / hoja del fabricante. */
    val socModel: String,
    /** Nombre comercial, p. ej. "Snapdragon 6s Gen 3". */
    val marketingName: String,
    /** Fabricante del SoC. */
    val vendor: String,
    /** Codenames de plataforma (Build.HARDWARE / BOARD), p. ej. "blair". */
    val codenames: List<String>,
    val clusters: ClusterSpec,
    val gpu: String,
    val dsp: DspInfo,
    val config: RecommendedAudioConfig,
    /** Fuente de los datos de núcleos/frecuencias/DSP. */
    val source: EvidenceKind,
    /** Fuente de la configuración recomendada (casi siempre ESTIMATED). */
    val configSource: EvidenceKind,
    /** Nota de procedencia, p. ej. "medido en Moto G85, deriva −6.25 ppm". */
    val note: String,
)

object HardwareKnowledgeBase {
    /** Versión de la base (para invalidar perfiles guardados con bases viejas). */
    const val VERSION = 1

    private fun hexagon(detail: String) = DspInfo(DspKind.HEXAGON, detail, offloadAvailable = false)
    private fun apu(detail: String) = DspInfo(DspKind.MTK_APU, detail, offloadAvailable = false)
    private fun exynosNpu(detail: String) = DspInfo(DspKind.EXYNOS_NPU, detail, offloadAvailable = false)
    private fun tensorTpu(detail: String) = DspInfo(DspKind.TENSOR_TPU, detail, offloadAvailable = false)
    private fun kirinNpu(detail: String) = DspInfo(DspKind.KIRIN_NPU, detail, offloadAvailable = false)

    // Búfer recomendado por clase: flagship 192, gama media-alta 256,
    // gama media 256, gama de entrada 512. ESTIMATED en todos: es una regla
    // derivada de la clase de SoC, no una medición por dispositivo.
    private val FLAGSHIP = RecommendedAudioConfig(bufferFrames = 192, audioThreads = 4, expectedLatencyMs = 12.0)
    private val UPPER_MID = RecommendedAudioConfig(bufferFrames = 256, audioThreads = 3, expectedLatencyMs = 16.0)
    private val MID = RecommendedAudioConfig(bufferFrames = 256, audioThreads = 2, expectedLatencyMs = 20.0)
    private val ENTRY = RecommendedAudioConfig(bufferFrames = 512, audioThreads = 2, expectedLatencyMs = 28.0)

    val ENTRIES: List<SocEntry> = listOf(
        // ── Moto G85 — EL dispositivo medido (fuente: repo IVANNA, datos en vivo) ──
        SocEntry(
            socModel = "SM6375-AC", marketingName = "Snapdragon 6s Gen 3", vendor = "Qualcomm",
            codenames = listOf("blair"),
            clusters = ClusterSpec(bigCores = 2, bigFreqMhz = 2300, littleCores = 6, littleFreqMhz = 2000),
            gpu = "Adreno 619", dsp = hexagon("Hexagon 692"),
            config = RecommendedAudioConfig(bufferFrames = 256, audioThreads = 2, expectedLatencyMs = 21.0),
            source = EvidenceKind.MEASURED, configSource = EvidenceKind.MEASURED,
            note = "Medido en Moto G85 (2026-11-07/09): AudioFlinger 21.00 ms; deriva AudioOut_D 47999.7 Hz (−6.25 ppm, dongle USB JieLi); 199 underruns / 523 overruns en 2.29M writes; HAL audio.primary.blair.so; 2 altavoces Dolby Atmos; doble mic; USB-C UAC2; sin jack 3.5 mm.",
        ),
        // ── Qualcomm Snapdragon 8 (flagship) ──
        SocEntry(
            socModel = "SM8650", marketingName = "Snapdragon 8 Gen 3", vendor = "Qualcomm",
            codenames = listOf("pineapple"),
            clusters = ClusterSpec(bigCores = 1, bigFreqMhz = 3300, midCores = 5, midFreqMhz = 3200, littleCores = 2, littleFreqMhz = 2300),
            gpu = "Adreno 750", dsp = hexagon("Hexagon NPU"),
            config = FLAGSHIP, source = EvidenceKind.VENDOR_DOC, configSource = EvidenceKind.ESTIMATED,
            note = "Hoja pública Qualcomm: 1× X4 + 3× A720 + 2× A720 + 2× A520.",
        ),
        SocEntry(
            socModel = "SM8550", marketingName = "Snapdragon 8 Gen 2", vendor = "Qualcomm",
            codenames = listOf("kalama"),
            clusters = ClusterSpec(bigCores = 1, bigFreqMhz = 3200, midCores = 4, midFreqMhz = 2800, littleCores = 3, littleFreqMhz = 2000),
            gpu = "Adreno 740", dsp = hexagon("Hexagon"),
            config = FLAGSHIP, source = EvidenceKind.VENDOR_DOC, configSource = EvidenceKind.ESTIMATED,
            note = "Hoja pública Qualcomm: 1× X3 + 2× A715 + 2× A710 + 3× A510.",
        ),
        SocEntry(
            socModel = "SM8450", marketingName = "Snapdragon 8 Gen 1", vendor = "Qualcomm",
            codenames = listOf("taro"),
            clusters = ClusterSpec(bigCores = 1, bigFreqMhz = 3000, midCores = 3, midFreqMhz = 2500, littleCores = 4, littleFreqMhz = 1800),
            gpu = "Adreno 730", dsp = hexagon("Hexagon 780"),
            config = FLAGSHIP, source = EvidenceKind.VENDOR_DOC, configSource = EvidenceKind.ESTIMATED,
            note = "Hoja pública Qualcomm: 1× X2 + 3× A710 + 4× A510.",
        ),
        SocEntry(
            socModel = "SM8350", marketingName = "Snapdragon 888", vendor = "Qualcomm",
            codenames = listOf("lahaina"),
            clusters = ClusterSpec(bigCores = 1, bigFreqMhz = 2840, midCores = 3, midFreqMhz = 2420, littleCores = 4, littleFreqMhz = 1800),
            gpu = "Adreno 660", dsp = hexagon("Hexagon 780"),
            config = FLAGSHIP, source = EvidenceKind.VENDOR_DOC, configSource = EvidenceKind.ESTIMATED,
            note = "Hoja pública Qualcomm: 1× X1 + 3× A78 + 4× A55.",
        ),
        SocEntry(
            socModel = "SM8250-AC", marketingName = "Snapdragon 870", vendor = "Qualcomm",
            codenames = listOf("kona"),
            clusters = ClusterSpec(bigCores = 1, bigFreqMhz = 3200, midCores = 3, midFreqMhz = 2420, littleCores = 4, littleFreqMhz = 1800),
            gpu = "Adreno 650", dsp = hexagon("Hexagon 698"),
            config = FLAGSHIP, source = EvidenceKind.VENDOR_DOC, configSource = EvidenceKind.ESTIMATED,
            note = "Hoja pública Qualcomm: 1× A77 @3.2 + 3× A77 + 4× A55.",
        ),
        SocEntry(
            socModel = "SM8250", marketingName = "Snapdragon 865", vendor = "Qualcomm",
            codenames = listOf("kona"),
            clusters = ClusterSpec(bigCores = 1, bigFreqMhz = 2840, midCores = 3, midFreqMhz = 2420, littleCores = 4, littleFreqMhz = 1800),
            gpu = "Adreno 650", dsp = hexagon("Hexagon 698"),
            config = FLAGSHIP, source = EvidenceKind.VENDOR_DOC, configSource = EvidenceKind.ESTIMATED,
            note = "Hoja pública Qualcomm: 1× A77 + 3× A77 + 4× A55.",
        ),
        // ── Qualcomm Snapdragon 7 / 6 ──
        SocEntry(
            socModel = "SM7550", marketingName = "Snapdragon 7 Gen 3", vendor = "Qualcomm",
            codenames = listOf("crow"),
            clusters = ClusterSpec(bigCores = 1, bigFreqMhz = 2630, midCores = 3, midFreqMhz = 2400, littleCores = 4, littleFreqMhz = 1800),
            gpu = "Adreno 720", dsp = hexagon("Hexagon NPU"),
            config = UPPER_MID, source = EvidenceKind.VENDOR_DOC, configSource = EvidenceKind.ESTIMATED,
            note = "Hoja pública Qualcomm: 1× A715 @2.63 + 3× A715 + 4× A510.",
        ),
        SocEntry(
            socModel = "SM7750", marketingName = "Snapdragon 7+ Gen 2", vendor = "Qualcomm",
            codenames = listOf("crow"),
            clusters = ClusterSpec(bigCores = 1, bigFreqMhz = 2910, midCores = 3, midFreqMhz = 2490, littleCores = 4, littleFreqMhz = 1800),
            gpu = "Adreno 725", dsp = hexagon("Hexagon"),
            config = UPPER_MID, source = EvidenceKind.VENDOR_DOC, configSource = EvidenceKind.ESTIMATED,
            note = "Hoja pública Qualcomm: 1× A715 @2.91 + 3× A715 + 4× A510.",
        ),
        SocEntry(
            socModel = "SM7450", marketingName = "Snapdragon 7 Gen 1", vendor = "Qualcomm",
            codenames = listOf("cape"),
            clusters = ClusterSpec(bigCores = 1, bigFreqMhz = 2400, midCores = 3, midFreqMhz = 2360, littleCores = 4, littleFreqMhz = 1800),
            gpu = "Adreno 644", dsp = hexagon("Hexagon"),
            config = UPPER_MID, source = EvidenceKind.VENDOR_DOC, configSource = EvidenceKind.ESTIMATED,
            note = "Hoja pública Qualcomm: 1× A710 + 3× A710 + 4× A510.",
        ),
        SocEntry(
            socModel = "SM7325", marketingName = "Snapdragon 778G", vendor = "Qualcomm",
            codenames = listOf("yupik"),
            clusters = ClusterSpec(bigCores = 1, bigFreqMhz = 2400, midCores = 3, midFreqMhz = 2200, littleCores = 4, littleFreqMhz = 1900),
            gpu = "Adreno 642L", dsp = hexagon("Hexagon 770"),
            config = UPPER_MID, source = EvidenceKind.VENDOR_DOC, configSource = EvidenceKind.ESTIMATED,
            note = "Hoja pública Qualcomm: 1× A78 @2.4 + 3× A78 + 4× A55.",
        ),
        SocEntry(
            socModel = "SM6450", marketingName = "Snapdragon 6 Gen 1", vendor = "Qualcomm",
            codenames = listOf("parrot"),
            clusters = ClusterSpec(bigCores = 4, bigFreqMhz = 2200, littleCores = 4, littleFreqMhz = 1800),
            gpu = "Adreno 710", dsp = hexagon("Hexagon"),
            config = MID, source = EvidenceKind.VENDOR_DOC, configSource = EvidenceKind.ESTIMATED,
            note = "Hoja pública Qualcomm: 4× A78 + 4× A55.",
        ),
        SocEntry(
            socModel = "SM6375", marketingName = "Snapdragon 695", vendor = "Qualcomm",
            codenames = listOf("holi"),
            clusters = ClusterSpec(bigCores = 2, bigFreqMhz = 2200, littleCores = 6, littleFreqMhz = 1700),
            gpu = "Adreno 619", dsp = hexagon("Hexagon 692"),
            config = MID, source = EvidenceKind.VENDOR_DOC, configSource = EvidenceKind.ESTIMATED,
            note = "Hoja pública Qualcomm: 2× A78 + 6× A55. Misma familia que el 6s Gen 3 del G85.",
        ),
        // ── MediaTek Dimensity / Helio ──
        SocEntry(
            socModel = "MT6983", marketingName = "Dimensity 9000", vendor = "MediaTek",
            codenames = listOf("mt6983"),
            clusters = ClusterSpec(bigCores = 1, bigFreqMhz = 3050, midCores = 3, midFreqMhz = 2850, littleCores = 4, littleFreqMhz = 1800),
            gpu = "Mali-G710 MC10", dsp = apu("APU 590"),
            config = FLAGSHIP, source = EvidenceKind.VENDOR_DOC, configSource = EvidenceKind.ESTIMATED,
            note = "Hoja pública MediaTek: 1× X2 + 3× A710 + 4× A510.",
        ),
        SocEntry(
            socModel = "MT6895", marketingName = "Dimensity 8000", vendor = "MediaTek",
            codenames = listOf("mt6895"),
            clusters = ClusterSpec(bigCores = 4, bigFreqMhz = 2750, littleCores = 4, littleFreqMhz = 2000),
            gpu = "Mali-G610 MC6", dsp = apu("APU 580"),
            config = UPPER_MID, source = EvidenceKind.VENDOR_DOC, configSource = EvidenceKind.ESTIMATED,
            note = "Hoja pública MediaTek: 4× A78 + 4× A55.",
        ),
        SocEntry(
            socModel = "MT6893", marketingName = "Dimensity 8200", vendor = "MediaTek",
            codenames = listOf("mt6893"),
            clusters = ClusterSpec(bigCores = 1, bigFreqMhz = 3100, midCores = 3, midFreqMhz = 3000, littleCores = 4, littleFreqMhz = 2000),
            gpu = "Mali-G610 MC6", dsp = apu("APU 580"),
            config = UPPER_MID, source = EvidenceKind.VENDOR_DOC, configSource = EvidenceKind.ESTIMATED,
            note = "Hoja pública MediaTek: 1× A78 @3.1 + 3× A78 + 4× A55.",
        ),
        SocEntry(
            socModel = "MT6877", marketingName = "Dimensity 7050", vendor = "MediaTek",
            codenames = listOf("mt6877"),
            clusters = ClusterSpec(bigCores = 2, bigFreqMhz = 2600, littleCores = 6, littleFreqMhz = 2000),
            gpu = "Mali-G68 MC4", dsp = apu("APU 550"),
            config = MID, source = EvidenceKind.VENDOR_DOC, configSource = EvidenceKind.ESTIMATED,
            note = "Hoja pública MediaTek: 2× A78 + 6× A55.",
        ),
        SocEntry(
            socModel = "MT6789", marketingName = "Helio G99", vendor = "MediaTek",
            codenames = listOf("mt6789"),
            clusters = ClusterSpec(bigCores = 2, bigFreqMhz = 2200, littleCores = 6, littleFreqMhz = 2000),
            gpu = "Mali-G57 MC2", dsp = apu("APU (Helio)"),
            config = ENTRY, source = EvidenceKind.VENDOR_DOC, configSource = EvidenceKind.ESTIMATED,
            note = "Hoja pública MediaTek: 2× A76 + 6× A55.",
        ),
        // ── Samsung Exynos ──
        SocEntry(
            socModel = "S5E9840", marketingName = "Exynos 2100", vendor = "Samsung",
            codenames = listOf("exynos2100"),
            clusters = ClusterSpec(bigCores = 1, bigFreqMhz = 2900, midCores = 3, midFreqMhz = 2800, littleCores = 4, littleFreqMhz = 2200),
            gpu = "Mali-G78 MP14", dsp = exynosNpu("Exynos NPU"),
            config = FLAGSHIP, source = EvidenceKind.VENDOR_DOC, configSource = EvidenceKind.ESTIMATED,
            note = "Hoja pública Samsung: 1× X1 + 3× A78 + 4× A55.",
        ),
        SocEntry(
            socModel = "S5E9925", marketingName = "Exynos 2200", vendor = "Samsung",
            codenames = listOf("exynos2200"),
            clusters = ClusterSpec(bigCores = 1, bigFreqMhz = 2800, midCores = 3, midFreqMhz = 2520, littleCores = 4, littleFreqMhz = 1820),
            gpu = "Xclipse 920", dsp = exynosNpu("Exynos NPU"),
            config = FLAGSHIP, source = EvidenceKind.VENDOR_DOC, configSource = EvidenceKind.ESTIMATED,
            note = "Hoja pública Samsung: 1× X2 + 3× A78 + 4× A55.",
        ),
        // ── Google Tensor ──
        SocEntry(
            socModel = "GS201", marketingName = "Tensor G2", vendor = "Google",
            codenames = listOf("cloudripper"),
            clusters = ClusterSpec(bigCores = 2, bigFreqMhz = 2850, midCores = 2, midFreqMhz = 2350, littleCores = 4, littleFreqMhz = 1800),
            gpu = "Mali-G710 MP7", dsp = tensorTpu("Tensor TPU (Edge TPU)"),
            config = FLAGSHIP, source = EvidenceKind.VENDOR_DOC, configSource = EvidenceKind.ESTIMATED,
            note = "Hoja pública Google: 2× X1 + 2× A78 + 4× A55.",
        ),
        SocEntry(
            socModel = "GS201", marketingName = "Tensor G3", vendor = "Google",
            codenames = listOf("zuma"),
            clusters = ClusterSpec(bigCores = 1, bigFreqMhz = 2910, midCores = 4, midFreqMhz = 2370, littleCores = 4, littleFreqMhz = 1700),
            gpu = "Immortalis-G715s MC10", dsp = tensorTpu("Tensor TPU (Rio)"),
            config = FLAGSHIP, source = EvidenceKind.VENDOR_DOC, configSource = EvidenceKind.ESTIMATED,
            note = "Hoja pública Google: 1× X3 + 4× A715 + 4× A510. Se distingue del G2 por el codename 'zuma'.",
        ),
        // ── HiSilicon Kirin ──
        SocEntry(
            socModel = "Hi3690", marketingName = "Kirin 9000", vendor = "HiSilicon",
            codenames = listOf("hi3690"),
            clusters = ClusterSpec(bigCores = 1, bigFreqMhz = 3130, midCores = 3, midFreqMhz = 2540, littleCores = 4, littleFreqMhz = 2050),
            gpu = "Mali-G78 MP24", dsp = kirinNpu("Da Vinci NPU"),
            config = FLAGSHIP, source = EvidenceKind.VENDOR_DOC, configSource = EvidenceKind.ESTIMATED,
            note = "Hoja pública HiSilicon: 1× A77 @3.13 + 3× A77 + 4× A55.",
        ),
    )

    /** Perfil genérico por familia, para SoCs desconocidos (recomendación conservadora). */
    private val GENERIC_ARM64 = SocEntry(
        socModel = "GENERIC-ARM64", marketingName = "ARM64 genérico", vendor = "Desconocido",
        codenames = emptyList(),
        clusters = ClusterSpec(bigCores = 2, bigFreqMhz = 2000, littleCores = 6, littleFreqMhz = 1800),
        gpu = "Desconocida", dsp = DspInfo(DspKind.UNKNOWN, null, offloadAvailable = false),
        config = RecommendedAudioConfig(bufferFrames = 512, audioThreads = 2, expectedLatencyMs = 32.0),
        source = EvidenceKind.UNKNOWN, configSource = EvidenceKind.ESTIMATED,
        note = "Perfil genérico: el SoC no está en la base. Se usa la medición en vivo del perfilador y recomendaciones conservadoras (búfer grande, 2 hilos).",
    )

    /**
     * Resolución de un perfil medido a una entrada de la base:
     *  1. Coincidencia exacta por socModel (Build.SOC_MODEL).
     *  2. Coincidencia por codename (Build.HARDWARE / BOARD).
     *  3. Familia por prefijo del modelo (SM8/SM7/SM6 → Qualcomm; MT → MediaTek;
     *     S5E → Exynos; GS → Tensor; Hi36 → Kirin) con la entrada más cercana.
     *  4. Genérico ARM64.
     * Devuelve la entrada y cómo se resolvió (para que la auditoría lo reporte).
     */
    enum class MatchKind { EXACT_SOC, CODENAME, FAMILY, GENERIC }

    data class Match(val entry: SocEntry, val kind: MatchKind)

    fun lookup(profile: DeviceProfile): Match {
        val soc = profile.cpu.socModel?.trim().orEmpty()
        val hw = profile.android.hardware.lowercase()
        val board = profile.android.board.lowercase()
        val byCodename: SocEntry? = ENTRIES.firstOrNull { e ->
            e.codenames.any { c -> hw == c.lowercase() || board == c.lowercase() }
        }
        if (soc.isNotEmpty()) {
            val exact = ENTRIES.filter { it.socModel.equals(soc, ignoreCase = true) }
            when {
                // SocModel ambiguo (Tensor G2/G3 comparten GS201): el codename
                // de plataforma desempata; sin codename, la primera entrada.
                exact.size > 1 -> return Match(byCodename ?: exact.first(), if (byCodename != null) MatchKind.CODENAME else MatchKind.EXACT_SOC)
                exact.size == 1 -> return Match(exact.first(), MatchKind.EXACT_SOC)
            }
        }
        if (byCodename != null) return Match(byCodename, MatchKind.CODENAME)
        // Familia por prefijo del modelo de SoC.
        val familyEntry = when {
            soc.startsWith("SM8", ignoreCase = true) -> ENTRIES.firstOrNull { it.socModel == "SM8650" }
            soc.startsWith("SM7", ignoreCase = true) -> ENTRIES.firstOrNull { it.socModel == "SM7550" }
            soc.startsWith("SM6", ignoreCase = true) -> ENTRIES.firstOrNull { it.socModel == "SM6450" }
            soc.startsWith("MT69", ignoreCase = true) -> ENTRIES.firstOrNull { it.socModel == "MT6895" }
            soc.startsWith("MT68", ignoreCase = true) -> ENTRIES.firstOrNull { it.socModel == "MT6877" }
            soc.startsWith("MT67", ignoreCase = true) -> ENTRIES.firstOrNull { it.socModel == "MT6789" }
            soc.startsWith("S5E", ignoreCase = true) -> ENTRIES.firstOrNull { it.socModel == "S5E9925" }
            soc.startsWith("GS", ignoreCase = true) -> ENTRIES.firstOrNull { it.socModel == "GS201" }
            soc.startsWith("Hi36", ignoreCase = true) -> ENTRIES.firstOrNull { it.socModel == "Hi3690" }
            else -> null
        }
        if (familyEntry != null) return Match(familyEntry, MatchKind.FAMILY)
        return Match(GENERIC_ARM64, MatchKind.GENERIC)
    }

    /** Búsqueda directa por modelo de SoC (para tests y diagnóstico). */
    fun bySocModel(socModel: String): SocEntry? =
        ENTRIES.firstOrNull { it.socModel.equals(socModel, ignoreCase = true) }
}
