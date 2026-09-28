package com.ivanna.omega.spatial

import android.content.Context
import android.util.Log
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/**
 * AbxResultStore — persistencia y exportación de trials ABX.
 *
 * Cada trial {condition, correct, timestampMs, ...} se apila en
 * SharedPreferences como JSON array (sobrevive a rotación y reinicio).
 * exportJson() materializa el historial + resumen estadístico.
 */
object AbxResultStore {
    private const val TAG = "IVANNA.AbxStore"
    private const val PREFS = "abx_results"
    private const val KEY_TRIALS = "trials"

    fun record(context: Context, trial: JSONObject) {
        runCatching {
            val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            val arr = JSONArray(prefs.getString(KEY_TRIALS, "[]"))
            arr.put(trial)
            prefs.edit().putString(KEY_TRIALS, arr.toString()).apply()
        }.onFailure { Log.w(TAG, "record falló: ${it.message}") }
    }

    fun loadAll(context: Context): List<JSONObject> {
        return runCatching {
            val arr = JSONArray(context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                .getString(KEY_TRIALS, "[]"))
            (0 until arr.length()).map { arr.getJSONObject(it) }
        }.getOrDefault(emptyList())
    }

    fun stats(context: Context): Triple<Int, Int, Double> {
        val trials = loadAll(context)
        val hits = trials.count { it.optBoolean("correct") }
        val p = AbxStats.binomialTest(hits, trials.size)
        return Triple(trials.size, hits, p)
    }

    fun clear(context: Context) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .edit().remove(KEY_TRIALS).apply()
    }

    /** Exporta historial + resumen. Destino preferente /data/adb/ivanna_omega
     *  (si hay root); fallback garantizado a filesDir. Devuelve el File real. */
    fun exportJson(context: Context): File? {
        return runCatching {
            val trials = loadAll(context)
            val (n, hits, p) = stats(context)
            val (ciLo, ciHi) = AbxStats.wilsonInterval95(hits, n)
            val out = JSONObject().apply {
                put("protocol", "ABX_Static_vs_DynamicHRTF")
                put("trials", n)
                put("hits", hits)
                put("pValueBinomialExact", p)
                put("ci95Low", ciLo)
                put("ci95High", ciHi)
                put("verdict", if (n >= 10 && p < 0.05) "PERCEPTIBLE" else "NO DEMOSTRADO")
                put("exportedAtMs", System.currentTimeMillis())
                put("history", JSONArray().apply { trials.forEach { put(it) } })
            }
            val name = "abx_results_${System.currentTimeMillis()}.json"
            val adbDir = File("/data/adb/ivanna_omega")
            val dst = if (adbDir.isDirectory && adbDir.canWrite()) File(adbDir, name)
                      else File(context.filesDir, name)
            dst.writeText(out.toString(2))
            Log.i(TAG, "exportado: ${dst.absolutePath} (n=$n hits=$hits p=$p)")
            dst
        }.onFailure { Log.w(TAG, "export falló: ${it.message}") }.getOrNull()
    }
}
