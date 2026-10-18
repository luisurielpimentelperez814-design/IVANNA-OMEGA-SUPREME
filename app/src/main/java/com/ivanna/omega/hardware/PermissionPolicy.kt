package com.ivanna.omega.hardware

/**
 * Política pura (sin Android) para decidir qué hacer con un permiso de tiempo de ejecución.
 *
 * Android no distingue "nunca pedido" de "denegado para siempre": en ambos casos
 * `shouldShowRequestPermissionRationale` devuelve false. Por eso hace falta recordar si
 * ya se pidió alguna vez ([requestedBefore], lo persiste [PermissionGate]).
 *
 *  - concedido                          → [PermissionState.GRANTED]
 *  - denegado y el sistema sugiere explicar → [PermissionState.SHOW_RATIONALE] (se puede volver a pedir)
 *  - denegado, ya pedido y sin explicación  → [PermissionState.OPEN_SETTINGS] (denegado de forma permanente
 *    o revocado en Ajustes: pedir otra vez no muestra diálogo, hay que mandar al usuario a Ajustes)
 *  - nunca pedido                       → [PermissionState.ASK]
 *
 * Cubre también la revocación en uso: si el permiso estaba concedido y el usuario lo quita en Ajustes,
 * en la siguiente comprobación llega `granted=false` con `requestedBefore=true`.
 */
enum class PermissionState { GRANTED, ASK, SHOW_RATIONALE, OPEN_SETTINGS }

object PermissionPolicy {
    fun decide(granted: Boolean, shouldShowRationale: Boolean, requestedBefore: Boolean): PermissionState = when {
        granted -> PermissionState.GRANTED
        shouldShowRationale -> PermissionState.SHOW_RATIONALE
        requestedBefore -> PermissionState.OPEN_SETTINGS
        else -> PermissionState.ASK
    }

    /** Mensaje para el usuario cuando una función necesita el micrófono y no se puede pedir ya. */
    fun micDeniedMessage(state: PermissionState): String = when (state) {
        PermissionState.OPEN_SETTINGS ->
            "El micrófono está denegado. Actívalo en Ajustes › Aplicaciones › IVANNA-OMEGA › Permisos › Micrófono. " +
                "El resto de la app sigue funcionando."
        PermissionState.SHOW_RATIONALE ->
            "Esta función mide con el micrófono. Solo se usa mientras dura la medición."
        else -> "Se necesita el permiso de micrófono para esta función."
    }

    /**
     * Elige la primera frecuencia de captura que el sistema acepta.
     * [minBufferBytes] devuelve el resultado de `AudioRecord.getMinBufferSize` para esa frecuencia;
     * valores ≤ 0 (ERROR / ERROR_BAD_VALUE) significan que no se admite.
     */
    fun pickSupportedRate(candidates: List<Int>, minBufferBytes: (Int) -> Int): Int? =
        candidates.firstOrNull { minBufferBytes(it) > 0 }
}
