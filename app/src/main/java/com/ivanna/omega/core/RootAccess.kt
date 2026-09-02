package com.ivanna.omega.core

import android.content.Context
import android.util.Log
import java.io.File

/**
 * RootAccess — deteccion real de root/Magisk, cacheada.
 *
 * PROBLEMA QUE ARREGLA:
 *   En todo el repo NO existia ni una sola comprobacion de root
 *   (grep de "su", "isRoot", "Runtime.getRuntime" en *.kt = 0 hits).
 *   Lo unico parecido era NoRootAudioProcessor.hasMagisk(), que mira
 *   /data/adb/magisk y /sbin/magisk: rutas que una app sin root NO puede
 *   stat() (EACCES) -> exists() devuelve false SIEMPRE, incluso en un
 *   dispositivo rooteado. Resultado: la app no sabia nunca en que modo
 *   estaba corriendo y el camino "sin root" quedaba muerto.
 *
 * ESTRATEGIA (de mas fiable a menos):
 *   1. Probe real: ejecutar `su -c id` con timeout y comprobar uid=0.
 *      Es el unico testigo que no miente. Se hace UNA vez por proceso.
 *   2. Binario `su` visible en el PATH tipico (no requiere root para stat).
 *   3. App de gestion de Magisk/KernelSU instalada (PackageManager).
 *   4. Props del modulo (persist.ivanna.magisk_active) via MagiskBridge.
 *
 * NOTA: probeSu() puede tardar (dialogo de concesion de Magisk). NUNCA
 * llamarla desde el hilo principal — de ahi los @WorkerThread implicitos y
 * el cache: la UI consulta `cachedRoot`, que es no-bloqueante.
 */
object RootAccess {

    private const val TAG = "IVANNA-RootAccess"
    private const val SU_TIMEOUT_MS = 3_000L

    private val SU_PATHS = listOf(
        "/system/bin/su", "/system/xbin/su", "/sbin/su",
        "/su/bin/su", "/system/sbin/su", "/vendor/bin/su",
        "/debug_ramdisk/su"
    )

    private val MANAGER_PACKAGES = listOf(
        "com.topjohnwu.magisk",
        "io.github.huskydg.magisk",
        "me.weishu.kernelsu",
        "com.kernelsu.manager",
        "eu.chainfire.supersu"
    )

    @Volatile private var probed = false
    @Volatile private var probeResult = false

    /** Ultimo resultado conocido, no bloqueante. false hasta que se hace probe. */
    val cachedRoot: Boolean get() = probeResult

    /** true si ya se ejecuto el probe real en este proceso. */
    val isProbed: Boolean get() = probed

    /** Binario su presente en una ruta legible sin privilegios. */
    fun suBinaryVisible(): Boolean = SU_PATHS.any { runCatching { File(it).exists() }.getOrDefault(false) }

    /** Gestor de root instalado (pista, no prueba). */
    fun managerInstalled(context: Context): Boolean {
        val pm = context.packageManager
        return MANAGER_PACKAGES.any { pkg ->
            runCatching { pm.getPackageInfo(pkg, 0) != null }.getOrDefault(false)
        }
    }

    /**
     * Probe real y bloqueante. Lanza `su -c id`, espera con timeout y
     * comprueba uid=0. Cachea el resultado para el resto del proceso.
     * DEBE llamarse en un hilo de background.
     */
    fun probeSu(force: Boolean = false): Boolean {
        if (probed && !force) return probeResult
        val ok = runCatching {
            val proc = ProcessBuilder("su", "-c", "id")
                .redirectErrorStream(true)
                .start()
            val deadline = System.currentTimeMillis() + SU_TIMEOUT_MS
            while (proc.isAlive && System.currentTimeMillis() < deadline) {
                Thread.sleep(50)
            }
            if (proc.isAlive) {
                proc.destroy()
                false
            } else {
                val out = proc.inputStream.bufferedReader().use { it.readText() }
                proc.exitValue() == 0 && out.contains("uid=0")
            }
        }.getOrElse { false }
        probed = true
        probeResult = ok
        Log.i(TAG, if (ok) "root disponible (su probe OK)" else "sin root (su probe fallo)")
        return ok
    }

    /** Resumen legible para la UI/telemetria. */
    fun describe(context: Context): String = buildString {
        append(if (cachedRoot) "ROOT" else if (probed) "NO-ROOT" else "ROOT?")
        append(" | su=").append(suBinaryVisible())
        append(" | manager=").append(managerInstalled(context))
    }
}
