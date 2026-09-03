package com.ivanna.omega.core

import android.content.Context
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

class IvannaCognitiveCore(
    private val context: Context
) {

    private var configured: Boolean = false

    suspend fun configure(
        apiKey: String? = null
    ): Boolean = withContext(Dispatchers.IO) {

        configured = !apiKey.isNullOrBlank()

        configured
    }


    fun isConfigured(): Boolean {
        return configured
    }


    suspend fun processQuery(
        query: String,
        contextWindow: List<String> = emptyList()
    ): String = withContext(Dispatchers.Default) {

        if (!configured) {
            return@withContext "Ivanna no está configurada."
        }

        when {
            query.contains("audio", ignoreCase = true) ->
                "Analizando motor DSP, perfiles acústicos y procesamiento adaptativo."

            query.contains("preset", ignoreCase = true) ->
                "Creando preset adaptativo."

            else ->
                "Procesando consulta: $query"
        }
    }
}
