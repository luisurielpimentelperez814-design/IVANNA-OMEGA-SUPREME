package com.ivanna.omega.core

import android.content.Context
import android.util.Log
import java.io.File

/**
 * IvannaAssetProvider — acceso sin root a los modelos SAF / SOFA / HRTF / RIR
 * empaquetados en el APK (assets/ivanna_omega/).
 *
 * Los motores nativos leen rutas de fichero; los assets del APK no son
 * ficheros, asi que este proveedor los extrae una sola vez a filesDir y
 * devuelve rutas reales. Orden de resolucion:
 *   1) /system/etc/ivanna_omega (modulo Magisk, si existe)
 *   2) filesDir/ivanna_omega (extraccion de assets, sin root)
 */
object IvannaAssetProvider {
    private const val TAG = "IvannaAssetProvider"
    private const val MODULE_DIR = "/system/etc/ivanna_omega"
    private const val ASSET_ROOT = "ivanna_omega"

    @Volatile private var extractedRoot: File? = null
    private val bgRunning = java.util.concurrent.atomic.AtomicBoolean(false)

    private fun versionCodeOf(context: Context): Long = runCatching {
        val pi = context.packageManager.getPackageInfo(context.packageName, 0)
        if (android.os.Build.VERSION.SDK_INT >= 28) pi.longVersionCode else @Suppress("DEPRECATION") pi.versionCode.toLong()
    }.getOrDefault(0L)

    /** Copia un asset a `out` via fichero temporal + rename (nunca queda a medias). */
    @JvmStatic
    fun copyAssetAtomic(context: Context, assetPath: String, out: File): Boolean {
        if (out.exists() && out.length() > 0L) return true
        return runCatching {
            out.parentFile?.mkdirs()
            val tmp = File(out.parentFile, out.name + ".part")
            context.assets.open(assetPath).use { i -> tmp.outputStream().use { o -> i.copyTo(o) } }
            tmp.renameTo(out) || (tmp.delete().let { false })
        }.onFailure { Log.w(TAG, "copy $assetPath: ${it.message}") }.getOrDefault(false)
    }

    /** Copia recursiva y completa de un arbol de assets (bloqueante; llamar fuera del hilo UI). */
    @JvmStatic
    fun copyTreeBlocking(context: Context, assetPath: String, dstDir: File) =
        copyAssetTree(context, assetPath, dstDir)

    @Synchronized
    fun modelDir(context: Context): File {
        val sys = File(MODULE_DIR)
        if (sys.isDirectory) return sys
        extractedRoot?.let { return it }
        val dst = File(context.filesDir, ASSET_ROOT)
        // La extraccion es COMPLETA y verificable: un marcador por versionCode se
        // escribe solo al terminar. Antes se daba por buena la carpeta si existia
        // kemar.ihr1, asi que un proceso muerto a mitad de la copia en segundo plano
        // dejaba SOFA/HRTF/RIR incompletos para siempre ("datasets no se despliegan").
        val marker = File(dst, ".extract_complete_v${versionCodeOf(context)}")
        if (!marker.exists()) {
            dst.mkdirs()
            val priorityRel = listOf(
                "SAF_model_total.json",
                "pca_basis.bin",
                "hrtf/kemar.ihr1"
            )
            for (rel in priorityRel) copyAssetAtomic(context, "$ASSET_ROOT/$rel", File(dst, rel))
            // El RIR (~2 MB, 200 salas) se copia COMPLETO y en sincronia: el loader
            // nativo indexa lo que encuentre en disco y con solo 5 WAV marcaba
            // "cargado" con 5 salas.
            copyAssetTree(context, "$ASSET_ROOT/rir", File(dst, "rir"))
            if (bgRunning.compareAndSet(false, true)) {
                Thread({
                    try {
                        copyAssetTree(context, ASSET_ROOT, dst)
                        marker.parentFile?.listFiles { f -> f.name.startsWith(".extract_complete_") }
                            ?.forEach { it.delete() }
                        marker.writeText("ok")
                    } catch (t: Throwable) {
                        Log.w(TAG, "extraccion en segundo plano incompleta: ${t.message}")
                    } finally { bgRunning.set(false) }
                }, "IvannaAssetBgExtract").start()
            }
        }
        extractedRoot = dst
        return dst
    }

    fun safModel(context: Context): File = File(modelDir(context), "SAF_model_total.json")
    fun hrtfDir(context: Context): File = File(modelDir(context), "hrtf")
    fun rirDir(context: Context): File = File(modelDir(context), "rir")
    fun sofaDir(context: Context): File = File(modelDir(context), "sofa")

    private fun copyAssetTree(context: Context, assetPath: String, dstDir: File) {
        val am = context.assets
        val entries = am.list(assetPath).orEmpty()
        if (entries.isEmpty()) {
            copyAssetAtomic(context, assetPath, File(dstDir.parentFile, dstDir.name))
            return
        }
        dstDir.mkdirs()
        for (name in entries) {
            copyAssetTree(context, "$assetPath/$name", File(dstDir, name))
        }
    }
}
