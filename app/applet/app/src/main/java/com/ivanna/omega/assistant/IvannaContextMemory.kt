package com.ivanna.omega.assistant

import android.content.Context
import java.util.concurrent.CopyOnWriteArrayList

/**
 * IvannaContextMemory — memoria de sesión ligera del asistente conversacional.
 *
 * Distinta de ai.memory.IvannaMemoryArchitecture (memoria cognitiva de 4 capas
 * con persistencia cifrada): esta clase es la memoria DE TRABAJO del panel —
 * escena actual, última explicación y ajustes aplicados durante la sesión.
 *
 * FIX (CI rojo): IvannaAssistant la construía con `IvannaContextMemory(appContext)`
 * y usaba recordAdjustment()/lastScene/lastExplanation/clearAll(), miembros que
 * no existían — la clase era un stub (solo add/getAll/clear y sin constructor
 * con Context). Se restaura el contrato completo que el asistente consume,
 * manteniendo la API original (add/getAll/clear) por compatibilidad.
 */
class IvannaContextMemory(@Suppress("unused") context: Context) {

    /** Escena acústica activa en la sesión (VOICE, MUSIC, UNKNOWN…). */
    @Volatile var lastScene: String? = null

    /** Última explicación hablada por IVANNA (para "¿qué hiciste?"). */
    @Volatile var lastExplanation: String? = null

    data class AdjustmentRecord(
        val command: String,
        val detail: String,
        val applied: Boolean,
        val timestampMs: Long = System.currentTimeMillis()
    )

    private val memory = CopyOnWriteArrayList<String>()
    private val adjustments = CopyOnWriteArrayList<AdjustmentRecord>()

    fun add(value: String) {
        memory.add(value)
    }

    fun getAll(): List<String> {
        return memory.toList()
    }

    fun clear() {
        memory.clear()
    }

    /** Registra un ajuste aplicado (o intentado) durante la sesión. */
    fun recordAdjustment(command: String, detail: String, applied: Boolean) {
        adjustments.add(AdjustmentRecord(command, detail, applied))
    }

    /** Historial de ajustes de la sesión, del más antiguo al más reciente. */
    fun getAdjustments(): List<AdjustmentRecord> = adjustments.toList()

    /** Limpia TODO el estado de sesión (usado por IvannaAssistant.clearMemory). */
    fun clearAll() {
        memory.clear()
        adjustments.clear()
        lastScene = null
        lastExplanation = null
    }
}
