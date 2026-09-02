package com.ivanna.omega.saf

/**
 * SaFBridge — JNI interface to SaFOptimizer (Φ_SAF^∞).
 * Library loaded by IvannaNativeLib; no separate load needed.
 */
object SaFBridge {
    @JvmStatic external fun nativeSaFInit(jsonPath: String): Boolean
    @JvmStatic external fun nativeSaFFeedback(direction: Int, correct: Boolean)
    @JvmStatic external fun nativeSaFGetParams(): FloatArray?
    @JvmStatic external fun nativeSaFGetIteration(): Int
    @JvmStatic external fun nativeSaFReset()
    @JvmStatic external fun nativeSaFIsConverged(): Boolean
    @JvmStatic external fun nativeSaFGetError(): Float

    // FIX (persistencia calibración SAF): antes m_q vivía solo en RAM del
    // proceso nativo — cada reinicio de la app volvía a HRTF promedio (q=0)
    // sin importar cuántas veces el usuario hubiera calibrado. Estas dos
    // funciones guardan/cargan el vector q[7] + iteración en un archivo
    // propio (IVANNA_SAF_STATE_V1), separado de SAF_model.json (que es el
    // modelo de referencia de 214 sujetos, no el resultado personal).
    @JvmStatic external fun nativeSaFSaveState(path: String): Boolean
    @JvmStatic external fun nativeSaFLoadState(path: String): Boolean

    /**
     * Pista de sujeto inicial para Φ_SAF: el próximo nativeSaFInit parte del
     * sujeto elegido por geometría de pinna en vez del promedio poblacional.
     * Viaja al daemon por el socket (SET_PINNA_METRICS.subjectIndex) — no
     * requiere símbolo JNI nuevo.
     */
    fun setSubjectIndexHint(idx: Int) {
        runCatching {
            com.ivanna.omega.magisk.OmegaEngineBridge.sendCommand(
                org.json.JSONObject().apply {
                    put("action", "SET_PINNA_METRICS")
                    put("subjectIndex", idx)
                    put("timestamp", System.currentTimeMillis())
                })
        }
    }
}
