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

    @Synchronized
    fun modelDir(context: Context): File {
        val sys = File(MODULE_DIR)
        if (sys.isDirectory) return sys
        extractedRoot?.let { return it }
        val dst = File(context.filesDir, ASSET_ROOT)
        if (!dst.isDirectory || !File(dst, "hrtf/kemar.ihr1").exists()) {
            dst.mkdirs()
            val priorityRel = listOf(
                "SAF_model_total.json",
                "pca_basis.bin",
                "hrtf/kemar.ihr1",
                "rir/metadata.csv",
                "rir/rir_0051.wav",
                "rir/rir_0122.wav",
                "rir/rir_0169.wav",
                "rir/rir_0081.wav",
                "rir/rir_0063.wav"
            )
            for (rel in priorityRel) {
                val out = File(dst, rel)
                if (!out.exists() || out.length() == 0L) {
                    out.parentFile?.mkdirs()
                    runCatching {
                        context.assets.open("$ASSET_ROOT/$rel").use { input ->
                            out.outputStream().use { o -> input.copyTo(o) }
                        }
                    }
                }
            }
            Thread({
                runCatching { copyAssetTree(context, ASSET_ROOT, dst) }
            }, "IvannaAssetBgExtract").start()
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
            runCatching {
                am.open(assetPath).use { input ->
                    File(dstDir.parentFile, dstDir.name).outputStream().use { out -> input.copyTo(out) }
                }
            }.onFailure { Log.w(TAG, "copy $assetPath: ${it.message}") }
            return
        }
        dstDir.mkdirs()
        for (name in entries) {
            copyAssetTree(context, "$assetPath/$name", File(dstDir, name))
        }
    }
}
