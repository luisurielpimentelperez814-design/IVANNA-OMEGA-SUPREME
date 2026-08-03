package com.ivanna.omega.magisk

import android.util.Log
import java.io.BufferedReader
import java.io.File
import java.io.InputStreamReader
import java.util.concurrent.TimeUnit

/**
 * MagiskBridge v2.1 (PATCH)
 *
 * - Lectura de props sin root mediante SystemProperties reflection.
 * - Cache de estados para evitar llamadas su repetidas.
 * - Detección daemon por persist.ivanna.daemon_active.
 * - Root únicamente para comandos reales al socket.
 */
object MagiskBridge {

    private const val TAG = "IVANNA-MagiskBridge"

    private const val SOCKET_OMEGA = "omega_daemon_socket"
    private const val SOCKET_LEGACY = "/data/pf/pf.sock"

    private const val PROP_ACTIVE = "persist.ivanna.magisk_active"
    private const val PROP_VERSION = "persist.ivanna.version"
    private const val PROP_DAEMON = "persist.ivanna.daemon_active"
    private const val PROP_CONCERT = "ivanna.concert_mode"

    private const val TIMEOUT_MS = 3000L
    private const val CACHE_TTL_MS = 2000L

    private data class Cached(
        val value: String,
        val stamp: Long
    )

    private val propCache = HashMap<String, Cached>()

    private fun getPropCached(key: String): String {
        val now = System.currentTimeMillis()
        val cached = propCache[key]

        if (cached != null && now - cached.stamp < CACHE_TTL_MS) {
            return cached.value
        }

        val value = readSystemPropNoRoot(key)
        propCache[key] = Cached(value, now)

        return value
    }

    private fun readSystemPropNoRoot(key: String): String {
        return try {
            val cls = Class.forName("android.os.SystemProperties")
            val method = cls.getMethod(
                "get",
                String::class.java,
                String::class.java
            )

            (method.invoke(null, key, "") as? String).orEmpty()

        } catch (t: Throwable) {
            Log.w(TAG, "SystemProperties error $key: ${t.message}")
            ""
        }
    }

    val isModuleActive: Boolean
        get() = getPropCached(PROP_ACTIVE) == "1"

    val moduleVersion: String
        get() = getPropCached(PROP_VERSION)
            .ifEmpty { "unknown" }


    val isDaemonRunning: Boolean
        get() {

            if (getPropCached(PROP_DAEMON) == "1") {
                return true
            }

            return try {
                isOmegaSocketAvailable()
            } catch (_: Throwable) {
                false
            }
        }


    fun sendCommand(command: String): String {

        val socket = when {

            isOmegaSocketAvailable() ->
                SOCKET_OMEGA

            File(SOCKET_LEGACY).exists() ->
                SOCKET_LEGACY

            else -> {

                setSystemProp(
                    "ivanna.pending_cmd",
                    command
                )

                Log.w(
                    TAG,
                    "Daemon offline queued $command"
                )

                return "queued"
            }
        }


        return try {

            val result =
                exec("echo -n '$command' | nc -U $socket")

            Log.d(
                TAG,
                "CMD=$command RESP=${result.result}"
            )

            result.result

        } catch (e: Exception) {

            Log.e(
                TAG,
                "Socket error",
                e
            )

            ""
        }
    }


    fun setPreset(name:String)=
        sendCommand("SET_PRESET:$name")

    fun getStatus() =
        sendCommand("STATUS")

    fun getTelemetry() =
        sendCommand("GET_TELEMETRY")

    fun reloadParams() =
        sendCommand("RELOAD_PARAMS")


    fun setBypass(v:Boolean)=
        sendCommand(
            "SET_BYPASS:${if(v)1 else 0}"
        )


    fun setDrive(v:Float)=sendCommand("SET_PF_DRIVE:$v")
    fun setWet(v:Float)=sendCommand("SET_PF_WET:$v")
    fun setMix(v:Float)=sendCommand("SET_PF_MIX:$v")
    fun setAlpha(v:Float)=sendCommand("SET_PF_ALPHA:$v")
    fun setBeta(v:Float)=sendCommand("SET_PF_BETA:$v")
    fun setGamma(v:Float)=sendCommand("SET_PF_GAMMA:$v")
    fun setFreq(v:Float)=sendCommand("SET_PF_FREQ:$v")
    fun setResonance(v:Float)=sendCommand("SET_PF_RESONANCE:$v")
    fun setLow(v:Float)=sendCommand("SET_PF_LOW:$v")
    fun setMid(v:Float)=sendCommand("SET_PF_MID:$v")
    fun setHigh(v:Float)=sendCommand("SET_PF_HIGH:$v")
    fun setPresence(v:Float)=sendCommand("SET_PF_PRESENCE:$v")
    fun setMaster(v:Float)=sendCommand("SET_PF_MASTER:$v")


    fun setConcertMode(enabled:Boolean){

        if(enabled){

            setPreset("Spatial")
            sendCommand("SET_REVERB:0.7")
            setSystemProp(PROP_CONCERT,"1")

        }else{

            setPreset("Warm")
            sendCommand("SET_REVERB:0.0")
            setSystemProp(PROP_CONCERT,"0")
        }

        Log.i(
            TAG,
            "ConcertMode=$enabled"
        )
    }


    val isConcertModeActive:Boolean
        get() =
            getPropCached(PROP_CONCERT)=="1"


    private data class ProcResult(
        val result:String,
        val exitCode:Int
    )


    private fun exec(cmd:String):ProcResult{

        val process =
            Runtime.getRuntime()
                .exec(
                    arrayOf(
                        "su",
                        "-c",
                        cmd
                    )
                )


        val reader =
            BufferedReader(
                InputStreamReader(
                    process.inputStream
                )
            )


        val output =
            reader.readText()
                .trim()


        val finished =
            process.waitFor(
                TIMEOUT_MS,
                TimeUnit.MILLISECONDS
            )


        if(!finished){
            process.destroyForcibly()
        }


        return ProcResult(
            output,
            if(finished)
                process.exitValue()
            else
                -1
        )
    }


    private fun setSystemProp(
        key:String,
        value:String
    ){

        try{

            exec(
                "setprop $key $value"
            )

        }catch(e:Exception){

            Log.w(
                TAG,
                "setprop failed $key"
            )
        }
    }


    private fun isOmegaSocketAvailable():Boolean{

        return File(
            "/dev/socket/$SOCKET_OMEGA"
        ).exists()
        ||
        File(
            SOCKET_LEGACY
        ).exists()
    }
}
