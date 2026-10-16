package com.ivanna.omega.core

import android.content.Context
import android.util.Log
import java.io.File
import java.util.concurrent.atomic.AtomicBoolean

/**
 * IvannaAssetProvider — despliegue y localización de los datasets SAF / SOFA / HRTF / RIR.
 *
 * Los motores nativos leen rutas de fichero; los assets del APK no son ficheros, así que este proveedor los
 * extrae a filesDir/ivanna_omega y devuelve rutas reales. Orden de resolución POR DATASET (v2.5.31):
 *   1) /system/etc/ivanna_omega   (módulo Magisk)
 *   2) /data/adb/ivanna_omega     (desplegado por customize.sh; solo legible con root)
 *   3) filesDir/ivanna_omega      (extraído del APK, sin root)
 *
 * v2.5.31 — por qué «no desplegaban»:
 *  - El APK y el zip del módulo EXCLUÍAN los .sofa (corregido en build.gradle.kts y build.yml).
 *  - Si existía /system/etc/ivanna_omega (módulo viejo, sin SOFA) NO se extraía nada y la resolución no caía
 *    al APK. Ahora cada dataset se resuelve por separado y la extracción corre siempre.
 *  - La extracción en segundo plano no dejaba constancia de haber terminado: si el proceso moría a mitad,
 *    nunca se completaba (bastaba que existiera kemar.ihr1). Ahora: archivos .part + rename atómico, copia
 *    solo si el tamaño difiere y un marcador por versionCode escrito SOLO si no hubo fallos.
 */
object IvannaAssetProvider {
    private const val TAG = "IvannaAssetProvider"
    private const val MODULE_DIR = "/system/etc/ivanna_omega"
    private const val ROOT_DIR = "/data/adb/ivanna_omega"
    private const val ASSET_ROOT = "ivanna_omega"
    private const val MARKER_PREFIX = ".deployed_v"

    /** De dónde sale hoy cada dataset. */
    enum class Source { MODULE, ROOT, APK_EXTRACTED, MISSING }

    /** Estado verificable de un dataset (conteo real de archivos en disco frente a lo esperado en el APK). */
    data class DatasetState(
        val name: String,
        val source: Source,
        val dir: File?,
        val files: Int,
        val expected: Int,
        val bytes: Long
    ) {
        val complete: Boolean get() = files > 0 && (expected == 0 || files >= expected)
    }

    data class Status(
        val sofa: DatasetState,
        val hrtf: DatasetState,
        val rir: DatasetState,
        val saf: DatasetState,
        val extracting: Boolean,
        val deployedForThisVersion: Boolean
    )

    private val extracting = AtomicBoolean(false)

    @Volatile private var expectedCache: Map<String, Int>? = null

    private fun extractedRoot(context: Context) = File(context.filesDir, ASSET_ROOT)

    private fun versionCode(): Int = com.ivanna.omega.BuildConfig.VERSION_CODE

    private fun marker(context: Context) = File(extractedRoot(context), MARKER_PREFIX + versionCode())

    /** Compatibilidad: devuelve el directorio de modelos preferido y se asegura de que el despliegue esté en marcha. */
    @Synchronized
    fun modelDir(context: Context): File {
        ensureDeployed(context)
        val sys = File(MODULE_DIR)
        return if (sys.isDirectory) sys else extractedRoot(context)
    }

    /**
     * Despliega los assets a filesDir. Primero, de forma síncrona, lo mínimo para arrancar (SAF, PCA, KEMAR y 5
     * salas RIR); después, en un hilo, todo el árbol. Idempotente: no repite trabajo si el marcador de esta
     * versión existe, salvo force = true. Devuelve true si ya está todo desplegado para esta versión.
     */
    @Synchronized
    fun ensureDeployed(context: Context, force: Boolean = false): Boolean {
        val dst = extractedRoot(context)
        if (!force && marker(context).exists()) return true
        if (force) dst.listFiles { f -> f.name.startsWith(MARKER_PREFIX) }?.forEach { it.delete() }
        dst.mkdirs()
        for (rel in listOf(
            "SAF_model.json", "SAF_model_total.json", "pca_basis.bin", "hrtf/kemar.ihr1",
            "rir/metadata.csv", "rir/rir_0051.wav", "rir/rir_0122.wav", "rir/rir_0169.wav",
            "rir/rir_0081.wav", "rir/rir_0063.wav"
        )) copyAsset(context, "$ASSET_ROOT/$rel", File(dst, rel))

        if (extracting.compareAndSet(false, true)) {
            Thread({
                try {
                    val failures = copyTree(context, ASSET_ROOT, dst)
                    if (failures == 0) {
                        dst.listFiles { f -> f.name.startsWith(MARKER_PREFIX) }?.forEach { it.delete() }
                        runCatching { marker(context).createNewFile() }
                        Log.i(TAG, "Datasets desplegados en ${dst.absolutePath}")
                    } else {
                        Log.w(TAG, "Despliegue incompleto: $failures archivos fallaron (se reintentará en el próximo arranque)")
                    }
                } catch (t: Throwable) {
                    Log.w(TAG, "Despliegue falló: ${t.message}")
                } finally {
                    extracting.set(false)
                }
            }, "IvannaAssetBgExtract").start()
        }
        return false
    }

    /** Fuerza una reextracción completa (botón «Re-desplegar» del panel). */
    fun redeploy(context: Context) { ensureDeployed(context, force = true) }

    // ── Resolución por dataset ─────────────────────────────────────────────

    private fun candidates(context: Context, rel: String): List<File> = listOf(
        File(MODULE_DIR, rel), File(ROOT_DIR, rel), File(extractedRoot(context), rel)
    )

    private fun hasContent(dir: File): Boolean = dir.isDirectory && (dir.listFiles()?.any { it.length() > 0L || it.isDirectory } == true)

    private fun resolveDir(context: Context, rel: String): File =
        candidates(context, rel).firstOrNull { hasContent(it) } ?: File(extractedRoot(context), rel)

    fun hrtfDir(context: Context): File = resolveDir(context, "hrtf")
    fun rirDir(context: Context): File = resolveDir(context, "rir")
    fun sofaDir(context: Context): File = resolveDir(context, "sofa")

    /** SAF_model_total.json (compatibilidad con la API anterior). */
    fun safModel(context: Context): File = safFile(context, "SAF_model_total.json") ?: File(extractedRoot(context), "SAF_model_total.json")

    /** SAF_model.json (214 sujetos, K = 7) — el que consume SaFBridge.nativeSaFInit. null si no está en ningún sitio. */
    fun safModelFile(context: Context): File? = safFile(context, "SAF_model.json")

    private fun safFile(context: Context, name: String): File? {
        candidates(context, name).firstOrNull { it.isFile && it.length() > 0L }?.let { return it }
        // Aún no extraído: se saca ese único archivo del APK ahora mismo (rápido) en vez de esperar al árbol.
        val out = File(extractedRoot(context), name)
        return if (copyAsset(context, "$ASSET_ROOT/$name", out)) out else null
    }

    // ── Estado verificable ─────────────────────────────────────────────────

    fun status(context: Context): Status {
        val exp = expected(context)
        fun state(name: String, rel: String, ext: String, expectedKey: String): DatasetState {
            val dir = candidates(context, rel).firstOrNull { hasContent(it) }
            if (dir == null) return DatasetState(name, Source.MISSING, null, 0, exp[expectedKey] ?: 0, 0L)
            var n = 0; var b = 0L
            dir.walkTopDown().forEach { if (it.isFile && it.name.endsWith(ext, true) && it.length() > 0L) { n++; b += it.length() } }
            val src = when { dir.path.startsWith(MODULE_DIR) -> Source.MODULE; dir.path.startsWith(ROOT_DIR) -> Source.ROOT; else -> Source.APK_EXTRACTED }
            return DatasetState(name, src, dir, n, exp[expectedKey] ?: 0, b)
        }
        val safF = candidates(context, "SAF_model.json").firstOrNull { it.isFile && it.length() > 0L }
        val saf = if (safF == null) DatasetState("SAF", Source.MISSING, null, 0, 1, 0L)
        else DatasetState("SAF", when { safF.path.startsWith(MODULE_DIR) -> Source.MODULE; safF.path.startsWith(ROOT_DIR) -> Source.ROOT; else -> Source.APK_EXTRACTED },
            safF.parentFile, 1, 1, safF.length())
        return Status(
            sofa = state("SOFA", "sofa", ".sofa", "sofa"),
            hrtf = state("HRTF", "hrtf", ".ihr1", "hrtf"),
            rir = state("RIR", "rir", ".wav", "rir"),
            saf = saf,
            extracting = extracting.get(),
            deployedForThisVersion = marker(context).exists()
        )
    }

    /** Archivos esperados por dataset según lo EMPAQUETADO en el APK (solo metadatos, sin leer contenido). */
    private fun expected(context: Context): Map<String, Int> {
        expectedCache?.let { return it }
        fun count(path: String, ext: String): Int {
            val entries = runCatching { context.assets.list(path) }.getOrNull().orEmpty()
            var n = 0
            for (e in entries) {
                val child = "$path/$e"
                n += if (e.endsWith(ext, true)) 1 else if (e.contains('.')) 0 else count(child, ext)
            }
            return n
        }
        val m = mapOf(
            "sofa" to count("$ASSET_ROOT/sofa", ".sofa"),
            "hrtf" to count("$ASSET_ROOT/hrtf", ".ihr1"),
            "rir" to count("$ASSET_ROOT/rir", ".wav")
        )
        expectedCache = m
        return m
    }

    // ── Copia robusta ──────────────────────────────────────────────────────

    /** Copia un asset a `out` (archivo .part + rename). Omite si ya existe con el mismo tamaño. */
    private fun copyAsset(context: Context, assetPath: String, out: File): Boolean = try {
        val size = context.assets.open(assetPath).use { it.available().toLong() }
        if (out.isFile && out.length() == size && size > 0L) true
        else {
            out.parentFile?.mkdirs()
            val part = File(out.parentFile, out.name + ".part")
            context.assets.open(assetPath).use { input -> part.outputStream().use { o -> input.copyTo(o) } }
            if (out.exists()) out.delete()
            part.renameTo(out)
        }
    } catch (t: Throwable) {
        Log.w(TAG, "copy $assetPath: ${t.message}")
        false
    }

    /** Devuelve el número de archivos que fallaron (0 = árbol completo). */
    private fun copyTree(context: Context, assetPath: String, dstDir: File): Int {
        val entries = runCatching { context.assets.list(assetPath) }.getOrNull().orEmpty()
        if (entries.isEmpty()) return 0
        dstDir.mkdirs()
        var failures = 0
        for (name in entries) {
            val child = "$assetPath/$name"
            val sub = runCatching { context.assets.list(child) }.getOrNull().orEmpty()
            failures += if (sub.isNotEmpty()) copyTree(context, child, File(dstDir, name))
                        else if (copyAsset(context, child, File(dstDir, name))) 0 else 1
        }
        return failures
    }
}
