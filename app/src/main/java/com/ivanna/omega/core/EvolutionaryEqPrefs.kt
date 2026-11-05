package com.ivanna.omega.core

import android.content.Context

/**
 * Interruptor del EQ evolutivo de 512 bandas en Ruta A (captura de reproduccion).
 *
 * El nativo arranca siempre con el interruptor en ON (paridad con Ruta B). Sin persistencia, apagarlo
 * se perdia al reiniciar la app y volvia a sonar sin que el usuario lo supiera. Este objeto guarda la
 * eleccion y la reaplica al arrancar (PersistedStateRestorer) y desde el panel.
 *
 * El interruptor es la INTENCION del usuario; que el filtro suene ademas exige que la calibracion
 * (hilo propio, unos segundos tras el arranque) haya verificado: eso lo informa
 * IvannaNativeLib.nativeIsEvolutionaryEqActive(), que no debe usarse para pintar el interruptor.
 */
object EvolutionaryEqPrefs {
    private const val FILE = "ivanna_ui_prefs"
    private const val KEY = "evo_eq_enabled"
    const val DEFAULT_ENABLED = true

    fun isEnabled(context: Context): Boolean =
        context.applicationContext.getSharedPreferences(FILE, Context.MODE_PRIVATE)
            .getBoolean(KEY, DEFAULT_ENABLED)

    /** Guarda la eleccion y la envia al nativo (si la biblioteca esta cargada). */
    fun setEnabled(context: Context, enabled: Boolean) {
        context.applicationContext.getSharedPreferences(FILE, Context.MODE_PRIVATE)
            .edit().putBoolean(KEY, enabled).apply()
        pushToNative(enabled)
    }

    /** Reenvia al nativo la eleccion guardada (arranque de la app / reinicio tras boot). */
    fun restore(context: Context) = pushToNative(isEnabled(context))

    private fun pushToNative(enabled: Boolean) {
        if (IvannaNativeLib.isLoaded) {
            runCatching { IvannaNativeLib.nativeSetEvolutionaryEqEnabled(enabled) }
        }
    }
}
