package com.ivanna.omega.dsp

import android.content.Context
import android.content.SharedPreferences
import android.util.Log

/**
 * DSPStatePrefs — persistencia atomica y validada del DSPState en SharedPreferences.
 *
 * ANTES: load() aceptaba cualquier Float (NaN/Infinity/rangos fuera de spec) porque
 * SharedPreferences no valida contenido. Un archivo corrupto por crash a mitad de
 * escritura, un downgrade, o una migracion mal hecha podian propagar valores
 * malignos al DSP nativo (que ya no los valida en el JNI de bajo nivel — de eso se
 * encarga ahora el punto de entrada en DSPState.pushToNative()). Ademas save()
 * usaba apply() (asincrono, "best effort"): si el proceso moria antes de que el
 * writer flusheara, la persistencia era una promesa vacia y el usuario perdia sus
 * cambios sin aviso — sintoma frecuente en dispositivos low-end que matan el proceso
 * al minuto en background.
 *
 * AHORA:
 *  - save() usa commit() (sincrono) + versionado de esquema (SCHEMA_VERSION), de modo
 *    que si el proceso muere despues del return, el estado ya esta en disco.
 *  - load() valida cada campo contra su rango real usando la misma politica que
 *    DSPState.pushToNative() (clamp + log si difiere), y si el bloque completo esta
 *    corrupto (JSON legacy o SchemaVersion mayor que la conocida) devuelve defaults en
 *    vez de crashear con ClassCastException.
 *  - hasAny() permite a IVANNAApplication saber si hay estado guardado sin cargar,
 *    para decidir si un preset de primera ejecucion (IVANNA_OMEGA_SIGNATURE) debe
 *    sembrarse.
 *
 * SSOT: este blob NO es la fuente de verdad del estado adaptativo — esa es
 * com.ivanna.omega.core.ParameterStore, ver AGENT_CLAIMS.md ("Controles y
 * Persistencia..."). DSPStatePrefs sirve al camino "raw DSP" (drive/wet/EQ/comp) que
 * el BootRestoreReceiver empuja al motor nativo tras reinicio.
 */
object DSPStatePrefs {
    private const val TAG = "DSPStatePrefs"
    private const val PREFS_NAME = "ivanna_dsp_state"
    private const val KEY_SCHEMA = "__schema_version"
    private const val CURRENT_SCHEMA = 1

    private fun prefs(context: Context): SharedPreferences =
        context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    fun hasAny(context: Context): Boolean = prefs(context).all.isNotEmpty()

    fun load(context: Context): DSPState {
        val p = prefs(context)
        val default = DSPState()

        // Version del esquema: si es mayor que la conocida, downgrade -> defaults.
        val version = try { p.getInt(KEY_SCHEMA, 0) } catch (_: ClassCastException) { 0 }
        if (version > CURRENT_SCHEMA) {
            Log.w(TAG, "schema $version desconocido (this build knows $CURRENT_SCHEMA) — cargando defaults")
            return default
        }

        // Guarda de tipo: en versiones antiguas alguna clave pudo escribirse con
        // tipo incorrecto (p.ej. anti_dolby escrito como Float donde ahora es
        // Boolean — patron ya observado en core/ParameterStore). Un getFloat sobre
        // Boolean lanza ClassCastException y tumba la restauracion de boot. Cada
        // acceso va en try/catch individual y cae al default de esa clave sin
        // afectar al resto.
        fun fl(k: String, d: Float, lo: Float, hi: Float): Float = try {
            val raw = p.getFloat(k, d)
            when {
                !raw.isFinite() -> {
                    Log.w(TAG, "$k: valor no finito en disco -> default $d")
                    d
                }
                raw < lo || raw > hi -> {
                    val clamped = raw.coerceIn(lo, hi)
                    Log.w(TAG, "$k=$raw fuera de [$lo,$hi] -> clamp $clamped")
                    clamped
                }
                else -> raw
            }
        } catch (t: Throwable) {
            Log.w(TAG, "$k: tipo incorrecto en disco (${t.javaClass.simpleName}) -> default $d")
            d
        }
        fun bo(k: String, d: Boolean): Boolean = try {
            p.getBoolean(k, d)
        } catch (t: Throwable) {
            Log.w(TAG, "$k: tipo incorrecto en disco (${t.javaClass.simpleName}) -> default $d")
            d
        }

        return DSPState(
            drive          = fl("drive",          default.drive,          0f,    4f),
            wet            = fl("wet",            default.wet,            0f,    1f),
            mix            = fl("mix",            default.mix,            0f,    1f),
            alpha          = fl("alpha",          default.alpha,          0f,    1f),
            beta           = fl("beta",           default.beta,           0f,    1f),
            gamma          = fl("gamma",          default.gamma,          0f,    1f),
            freq           = fl("freq",           default.freq,          20f, 20000f),
            resonance      = fl("resonance",      default.resonance,     0.1f,  10f),
            low            = fl("low",            default.low,          -18f,   18f),
            mid            = fl("mid",            default.mid,          -18f,   18f),
            high           = fl("high",           default.high,         -18f,   18f),
            presence       = fl("presence",       default.presence,     -18f,   18f),
            master         = fl("master",         default.master,       -18f,   18f),
            compThreshold  = fl("compThreshold",  default.compThreshold, -60f,   0f),
            compRatio      = fl("compRatio",      default.compRatio,      1f,   20f),
            exciterDrive   = fl("exciterDrive",   default.exciterDrive,   0f,    1f),
            stereoWidth    = fl("stereoWidth",    default.stereoWidth,    0f,    2f),
            makeupGain     = fl("makeupGain",     default.makeupGain,   -18f,   18f),
            bypass         = bo("bypass",         default.bypass)
        )
    }

    /**
     * Escritura sincrona (commit) — critica cuando el proceso puede morir sin aviso
     * (dispositivos low-end que matan la app en background, o crashes de otros
     * frentes). Si commit() devuelve false, se registra y no se miente al llamante.
     */
    fun save(context: Context, state: DSPState): Boolean {
        val ok = prefs(context).edit()
            .putInt(KEY_SCHEMA, CURRENT_SCHEMA)
            .putFloat("drive", state.drive)
            .putFloat("wet", state.wet)
            .putFloat("mix", state.mix)
            .putFloat("alpha", state.alpha)
            .putFloat("beta", state.beta)
            .putFloat("gamma", state.gamma)
            .putFloat("freq", state.freq)
            .putFloat("resonance", state.resonance)
            .putFloat("low", state.low)
            .putFloat("mid", state.mid)
            .putFloat("high", state.high)
            .putFloat("presence", state.presence)
            .putFloat("master", state.master)
            .putFloat("compThreshold", state.compThreshold)
            .putFloat("compRatio", state.compRatio)
            .putFloat("exciterDrive", state.exciterDrive)
            .putFloat("stereoWidth", state.stereoWidth)
            .putFloat("makeupGain", state.makeupGain)
            .putBoolean("bypass", state.bypass)
            .commit()
        if (!ok) Log.w(TAG, "save(): SharedPreferences.commit() devolvio false")
        return ok
    }
}
