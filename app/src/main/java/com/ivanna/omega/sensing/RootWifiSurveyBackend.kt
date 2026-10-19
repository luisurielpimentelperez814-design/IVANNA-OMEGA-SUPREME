package com.ivanna.omega.sensing

import android.os.SystemClock
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference

/**
 * Root-assisted Wi-Fi survey fallback for devices with Magisk and a compatible
 * iw utility. Commands are fixed/allow-listed; interface names are validated
 * before use. Runs only after explicit opt-in and while the caller's UI is
 * foregrounded. Root access does not guarantee driver support for CSI.
 */
class RootWifiSurveyBackend {
    data class Result(
        val rootGranted: Boolean,
        val iwAvailable: Boolean,
        val interfaceName: String?,
        val observations: List<WifiObservation>,
        val message: String,
        val elapsedMs: Long
    )

    suspend fun scanOnce(consentGranted: Boolean): Result = withContext(Dispatchers.IO) {
        val start = SystemClock.elapsedRealtime()
        if (!consentGranted) return@withContext result(false, false, null, emptyList(),
            "El escaneo root requiere consentimiento explícito.", start)

        val id = runRoot("id")
        if (id.exitCode != 0 || !id.stdout.contains("uid=0")) {
            return@withContext result(false, false, null, emptyList(),
                "Magisk/root no autorizado. Se mantiene el método Android estándar.", start)
        }

        val devs = runRoot("iw dev")
        if (devs.exitCode != 0) return@withContext result(true, false, null, emptyList(),
            "Root disponible, pero no se encontró una herramienta iw utilizable.", start)

        // Parse only interface names emitted by iw; never accept user-provided shell fragments.
        val iface = Regex("(?m)^\\s*Interface\\s+([A-Za-z0-9_.-]{1,15})\\s*$")
            .findAll(devs.stdout).map { it.groupValues[1] }.firstOrNull()
            ?: return@withContext result(true, true, null, emptyList(),
                "iw está presente, pero no anuncia una interfaz Wi-Fi controlable.", start)

        val scan = runRoot("iw dev $iface scan")
        if (scan.exitCode != 0) return@withContext result(true, true, iface, emptyList(),
            "El controlador rechazó el escaneo root; no se fabrican resultados.", start)

        val observations = parseScan(scan.stdout)
        result(true, true, iface, observations,
            "Escaneo root completado con iw. Señal/frecuencia son metadatos Wi-Fi; no son CSI ni anatomía.", start)
    }

    private data class CommandResult(val exitCode: Int, val stdout: String)

    /**
     * Enforce the deadline while output is being read. Reading the stream on the
     * caller thread before waitFor() can block forever and makes a timeout fake.
     */
    private fun runRoot(command: String): CommandResult {
        // All call sites pass constants or an interface validated by a strict regex.
        var process: Process? = null
        return try {
            val runningProcess = ProcessBuilder("su", "-c", command)
                .redirectErrorStream(true)
                .start()
            process = runningProcess
            val output = AtomicReference("")
            val reader = Thread({
                output.set(runCatching {
                    runningProcess.inputStream.bufferedReader().use { it.readText() }
                }.getOrDefault(""))
            }, "ivanna-root-wifi-output").apply {
                isDaemon = true
                start()
            }
            val finished = runningProcess.waitFor(12, TimeUnit.SECONDS)
            if (!finished) {
                runningProcess.destroy()
                if (runningProcess.isAlive) runningProcess.destroyForcibly()
                reader.join(250)
                CommandResult(124, output.get())
            } else {
                reader.join(500)
                CommandResult(runningProcess.exitValue(), output.get())
            }
        } catch (e: InterruptedException) {
            process?.destroyForcibly()
            Thread.currentThread().interrupt()
            CommandResult(124, "Comando root interrumpido.")
        } catch (e: Exception) {
            process?.destroyForcibly()
            CommandResult(127, e.javaClass.simpleName)
        }
    }

    private fun parseScan(output: String): List<WifiObservation> {
        val blocks = output.split(Regex("(?m)^BSS\\s+"))
        return blocks.drop(1).mapNotNull { block ->
            val bssid = block.lineSequence().firstOrNull()?.substringBefore('(')?.trim().orEmpty()
            val ssid = Regex("(?m)^\\s*SSID:\\s*(.*)$").find(block)?.groupValues?.get(1).orEmpty()
            val signal = Regex("(?m)^\\s*signal:\\s*(-?[0-9]+(?:\\.[0-9]+)?)\\s*dBm").find(block)
                ?.groupValues?.get(1)?.toDoubleOrNull()?.toInt() ?: return@mapNotNull null
            val frequency = Regex("(?m)^\\s*freq:\\s*([0-9]+)").find(block)
                ?.groupValues?.get(1)?.toIntOrNull() ?: 0
            WifiObservation(ssid.take(64).ifEmpty { if (bssid.isNotBlank()) "<red oculta>" else "" },
                signal, frequency, SystemClock.elapsedRealtime() * 1000L)
        }.distinctBy { Triple(it.ssid, it.frequencyMhz, it.rssiDbm) }
    }

    private fun result(root: Boolean, iw: Boolean, iface: String?, observations: List<WifiObservation>,
                       message: String, start: Long) = Result(
        rootGranted = root, iwAvailable = iw, interfaceName = iface,
        observations = observations, message = message,
        elapsedMs = SystemClock.elapsedRealtime() - start
    )
}
