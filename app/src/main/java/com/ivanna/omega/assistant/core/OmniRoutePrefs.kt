package com.ivanna.omega.assistant.core

import android.content.Context

/**
 * OmniRoutePrefs — ajustes del proveedor LLM OmniRoute (NO secretos).
 *
 * OmniRoute es el gateway de IA gratuito que corre en la VM de Muse
 * (https://github.com/ChrisCompton/omniroute, MIT, endpoint OpenAI-compatible).
 * La app lo usa como cerebro LLM alternativo a Gemini para el chat.
 *
 * - Gemini sigue siendo el DEFAULT; OmniRoute es opt-in (flag
 *   `omniroute_enabled`, apagado por defecto).
 * - La API key de OmniRoute NO vive aquí: va en
 *   SecureConfigurationManager (EncryptedSharedPreferences), igual que la
 *   key Gemini.
 * - Base URL por defecto: la IP Tailscale REAL de la VM, verificada con
 *   `tailscale ip` el 2026-11-14: 100.71.103.19 (NO la 100.83.138.49 que
 *   mencionaba el informe de investigación — esa no es esta VM).
 *   Puerto 20128, ruta /v1. El usuario puede cambiarla en ajustes.
 * - Modelo por defecto: el combo `ivanna-free` (fallback automático entre
 *   los proveedores gratuitos conectados en el gateway). Si el combo aún
 *   no existe en el gateway, el usuario puede poner un modelo directo
 *   (p. ej. `pol/claude`, sin key ni registro).
 *
 * Fail-closed: si OmniRoute falla o no responde, el agente devuelve
 * Offline con la última respuesta en caché (ver IvannaGeminiAgent).
 * El audio NUNCA depende del gateway.
 */
object OmniRoutePrefs {
    private const val PREFS_NAME = "ivanna_llm_provider"

    /** Base URL por defecto: gateway OmniRoute de la VM por Tailscale. */
    const val DEFAULT_BASE_URL = "http://100.71.103.19:20128/v1"

    /** Modelo/combo por defecto del gateway. */
    const val DEFAULT_MODEL = "ivanna-free"

    private fun prefs(context: Context) =
        context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    /** true = el chat usa OmniRoute; false (default) = Gemini. */
    fun isOmniRouteEnabled(context: Context): Boolean =
        prefs(context).getBoolean("omniroute_enabled", false)

    fun setOmniRouteEnabled(context: Context, enabled: Boolean) {
        prefs(context).edit().putBoolean("omniroute_enabled", enabled).apply()
    }

    /** Base URL del gateway (incluye /v1). Sin barra final. */
    fun getBaseUrl(context: Context): String =
        prefs(context).getString("omniroute_base_url", DEFAULT_BASE_URL)
            ?.trim()?.trimEnd('/')?.ifBlank { DEFAULT_BASE_URL } ?: DEFAULT_BASE_URL

    fun setBaseUrl(context: Context, url: String) {
        val clean = url.trim().trimEnd('/')
        prefs(context).edit()
            .putString("omniroute_base_url", clean.ifBlank { DEFAULT_BASE_URL })
            .apply()
    }

    /** Modelo o combo del gateway (p. ej. `ivanna-free`, `pol/claude`). */
    fun getModel(context: Context): String =
        prefs(context).getString("omniroute_model", DEFAULT_MODEL)
            ?.trim()?.ifBlank { DEFAULT_MODEL } ?: DEFAULT_MODEL

    fun setModel(context: Context, model: String) {
        val clean = model.trim()
        prefs(context).edit()
            .putString("omniroute_model", clean.ifBlank { DEFAULT_MODEL })
            .apply()
    }
}
