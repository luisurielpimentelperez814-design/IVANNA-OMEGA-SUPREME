// UiActionBridge.kt — puente universal de persistencia para controles de UI.
// (c) 2026 Luis Uriel Pimentel Perez — GORE TNS.
//
// Auditoria 2026-09-20 (barrido completo de pantallas): los controles con
// callbacks VACIOS (`= { }`) perdian su valor al salir de la pantalla o
// reiniciar la app — controles de adorno. Este objeto cierra la brecha: TODO
// callback interactivo persiste su valor en SharedPreferences con clave
// namespaced por pantalla y queda restaurado al reabrir. Los controles con
// efecto DSP siguen yendo por su ruta JNI normal — esto cubre PERSISTENCIA.
package com.ivanna.omega.core

import android.annotation.SuppressLint
import android.content.Context
import android.content.SharedPreferences

object UiActionBridge {
    private const val PREFS = "ivanna_ui_controls"

    @Volatile private var prefs: SharedPreferences? = null

    @SuppressLint("PrivateApi")
    private fun ctx(): Context? = runCatching {
        Class.forName("android.app.ActivityThread")
            .getMethod("currentApplication")
            .invoke(null) as? Context
    }.getOrNull()

    private fun store(): SharedPreferences? {
        if (prefs == null) synchronized(this) {
            if (prefs == null) prefs = ctx()?.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        }
        return prefs
    }

    fun persist(screen: String, control: String, value: Any?) {
        val s = store() ?: return
        val key = "$screen/$control"
        runCatching {
            when (value) {
                is Float   -> s.edit().putFloat(key, value).apply()
                is Int     -> s.edit().putInt(key, value).apply()
                is Boolean -> s.edit().putBoolean(key, value).apply()
                is Long    -> s.edit().putLong(key, value).apply()
                is String  -> s.edit().putString(key, value).apply()
                null       -> s.edit().putBoolean(key + "/touched", true).apply()
                else       -> s.edit().putString(key, value.toString()).apply()
            }
        }
    }

    fun onAction(screen: String, control: String) {
        val s = store() ?: return
        runCatching {
            s.edit()
                .putBoolean("$screen/$control/touched", true)
                .putLong("$screen/$control/ts", System.currentTimeMillis())
                .apply()
        }
    }

    fun readFloat(screen: String, control: String, def: Float): Float =
        runCatching { store()?.getFloat("$screen/$control", def) ?: def }.getOrDefault(def)
    fun readBoolean(screen: String, control: String, def: Boolean): Boolean =
        runCatching { store()?.getBoolean("$screen/$control", def) ?: def }.getOrDefault(def)
    fun readString(screen: String, control: String, def: String): String =
        runCatching { store()?.getString("$screen/$control", def) ?: def }.getOrDefault(def)

    fun dumpKeys(): Set<String> = store()?.all?.keys ?: emptySet()
}
