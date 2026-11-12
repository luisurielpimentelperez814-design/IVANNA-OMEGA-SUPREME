package com.ivanna.omega.tidal

import android.util.Base64
import android.util.Log
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import org.json.JSONObject
import java.net.URLEncoder
import java.nio.charset.StandardCharsets
import java.security.MessageDigest
import java.security.SecureRandom

/**
 * TIDAL — autenticación OAuth PKCE (v2.5.416).
 *
 * ÚNICO flujo implementado: PKCE (cliente público, SIN secreto).
 * El device flow de tidalapi exige `client_secret` en el intercambio de
 * token y ese valor NO se incrusta en IVANNA: no se copia ninguna
 * credencial confidencial de terceros al repo.
 *
 * - authorize: GET https://login.tidal.com/authorize?response_type=code
 *   &redirect_uri=https://tidal.com/android/login/auth&client_id=<PKCE>
 *   &lang=EN&appMode=android&client_unique_key=..&code_challenge=..
 *   &code_challenge_method=S256&restrict_signup=true
 * - token: POST https://auth.tidal.com/v1/oauth2/token con
 *   grant_type=authorization_code, code, client_id, redirect_uri,
 *   scope=r_usr+w_usr+w_sub, code_verifier, client_unique_key (SIN secreto)
 * - refresh: POST mismo endpoint con grant_type=refresh_token,
 *   refresh_token, client_id (SIN secreto). Si el servidor responde
 *   invalid_client, el refresh de este cliente exige secreto: se invalida
 *   la sesión y se pide re-login (peor caso: un re-login por semana).
 * - Tras el intercambio: GET sessions → sessionId, countryCode, userId.
 *
 * El login ocurre SOLO en el WebView con la página de Tidal: Esse escribe
 * SUS credenciales ahí; la app solo recibe el `code` del redirect.
 * Tokens SOLO en TidalTokenStore (cifrado). Nunca se loguean.
 */
open class TidalAuth(
    private val http: TidalHttp,
    private val store: TidalTokenStore,
    private val apiBase: String = "https://api.tidal.com/v1/"
) {

    companion object {
        private const val TAG = "IVANNA.TidalAuth"
        /** Identificador PÚBLICO del cliente PKCE (viaja en claro en toda URL de authorize). */
        const val PKCE_CLIENT_ID = "6BDSRdpK9hqEBTgU"
        const val REDIRECT_URI = "https://tidal.com/android/login/auth"
        const val AUTHORIZE_URL = "https://login.tidal.com/authorize"
        const val TOKEN_URL = "https://auth.tidal.com/v1/oauth2/token"
        const val SCOPE = "r_usr+w_usr+w_sub"
        /** Margen de expiración (tidalapi): refrescar 60 s antes de que caduque. */
        const val TOKEN_EXPIRY_MARGIN_MS: Long = 60_000L
    }

    sealed class AuthState {
        object SignedOut : AuthState()
        data class Authorizing(val authorizeUrl: String, val verifier: String, val uniqueKey: String) : AuthState()
        data class SignedIn(val userId: Long) : AuthState()
        object NeedsRelogin : AuthState()
    }

    private val _stateFlow = MutableStateFlow<AuthState>(initialState())
    /** Estado observable por Compose. */
    val stateFlow: StateFlow<AuthState> = _stateFlow.asStateFlow()

    val state: AuthState get() = _stateFlow.value

    private fun setState(s: AuthState) {
        _stateFlow.value = s
    }

    private val refreshLock = Any()

    private fun initialState(): AuthState {
        val tokens = store.load() ?: return AuthState.SignedOut
        return if (tokens.userId > 0) AuthState.SignedIn(tokens.userId) else AuthState.SignedOut
    }

    // ── PKCE ──────────────────────────────────────────────────────────────

    object Pkce {
        fun generateVerifier(): String {
            val bytes = ByteArray(32)
            SecureRandom().nextBytes(bytes)
            return base64Url(bytes)
        }

        fun challenge(verifier: String): String {
            val digest = MessageDigest.getInstance("SHA-256")
                .digest(verifier.toByteArray(StandardCharsets.UTF_8))
            return base64Url(digest)
        }

        fun generateClientUniqueKey(): String {
            val bytes = ByteArray(8)
            SecureRandom().nextBytes(bytes)
            return bytes.joinToString("") { "%02x".format(it.toInt() and 0xFF) }
        }

        fun buildAuthorizeUrl(verifier: String, uniqueKey: String): String {
            val challenge = challenge(verifier)
            val params = listOf(
                "response_type" to "code",
                "redirect_uri" to REDIRECT_URI,
                "client_id" to PKCE_CLIENT_ID,
                "lang" to "EN",
                "appMode" to "android",
                "client_unique_key" to uniqueKey,
                "code_challenge" to challenge,
                "code_challenge_method" to "S256",
                "restrict_signup" to "true"
            ).joinToString("&") { (k, v) ->
                "$k=${URLEncoder.encode(v, StandardCharsets.UTF_8.name())}"
            }
            return "$AUTHORIZE_URL?$params"
        }

        /** Extrae el `code` de la URL de redirect (o de una URL pegada a mano). */
        fun extractCodeFromRedirect(url: String): String? {
            if (!url.contains("tidal.com/android/login/auth")) return null
            val query = url.substringAfter("?", "")
            for (part in query.split("&")) {
                val kv = part.split("=", limit = 2)
                if (kv.size == 2 && kv[0] == "code" && kv[1].isNotBlank()) return kv[1]
            }
            return null
        }

        private fun base64Url(bytes: ByteArray): String =
            Base64.encodeToString(bytes, Base64.URL_SAFE or Base64.NO_PADDING or Base64.NO_WRAP)
    }

    // ── Flujo ─────────────────────────────────────────────────────────────

    /** Paso 1: genera el par PKCE y la URL de login para el WebView. */
    fun beginLogin(): AuthState.Authorizing {
        val verifier = Pkce.generateVerifier()
        val uniqueKey = Pkce.generateClientUniqueKey()
        val url = Pkce.buildAuthorizeUrl(verifier, uniqueKey)
        val auth = AuthState.Authorizing(url, verifier, uniqueKey)
        setState(auth)
        return auth
    }

    /** Paso 2: intercambia el `code` por tokens y carga la sesión. */
    fun completeLogin(code: String, verifier: String, uniqueKey: String): AuthState {
        val body = formBody(
            "grant_type" to "authorization_code",
            "code" to code,
            "client_id" to PKCE_CLIENT_ID,
            "redirect_uri" to REDIRECT_URI,
            "scope" to SCOPE,
            "code_verifier" to verifier,
            "client_unique_key" to uniqueKey
        )
        val tokens = exchangeToken(body)
        setState(AuthState.SignedIn(tokens.userId))
        return state
    }

    /** Access token válido (refresca si hace falta). null = hay que re-loguear. */
    open fun getValidAccessToken(): String? {
        val tokens = store.load() ?: run { setState(AuthState.SignedOut); return null }
        if (!tokens.isExpired(System.currentTimeMillis() + TOKEN_EXPIRY_MARGIN_MS)) {
            return tokens.accessToken
        }
        return refreshLocked(tokens)?.accessToken
    }

    fun signOut() {
        store.clear()
        setState(AuthState.SignedOut)
    }

    // ── Internos ──────────────────────────────────────────────────────────

    private fun refreshLocked(old: TidalTokens): TidalTokens? {
        synchronized(refreshLock) {
            // Otro hilo ya refrescó mientras esperábamos: usar lo nuevo.
            val current = store.load()
            if (current != null && current.accessToken != old.accessToken &&
                !current.isExpired(System.currentTimeMillis() + TOKEN_EXPIRY_MARGIN_MS)
            ) return current
            return runCatching {
                val body = formBody(
                    "grant_type" to "refresh_token",
                    "refresh_token" to old.refreshToken,
                    "client_id" to PKCE_CLIENT_ID
                )
                exchangeToken(body).also { setState(AuthState.SignedIn(it.userId)) }
            }.onFailure { ex ->
                Log.w(TAG, "Refresh falló (${ex.message}); se pide re-login")
                store.clear()
                setState(AuthState.NeedsRelogin)
            }.getOrNull()
        }
    }

    /**
     * Intercambio de token (código o refresh). Respuesta:
     * { access_token, refresh_token, expires_in, token_type }.
     * Errores: invalid_client → este cliente exige secreto en refresh
     * (re-login); invalid_grant → refresh revocado (re-login).
     */
    private fun exchangeToken(formBody: String): TidalTokens {
        val headers = mapOf("Content-Type" to "application/x-www-form-urlencoded")
        val resp = runCatching { http.request("POST", TOKEN_URL, headers, formBody) }
            .getOrElse { throw TidalApiException("Sin conexión con Tidal: ${it.message}", isRetryable = true) }
        val json = runCatching { JSONObject(resp.body) }.getOrNull()
        if (resp.status !in 200..299 || json == null) {
            val error = json?.optString("error", "") ?: ""
            val authError = error == "invalid_client" || error == "invalid_grant" ||
                resp.status == 401 || resp.status == 403
            throw TidalApiException(
                "Tidal rechazó el token (${error.ifEmpty { "HTTP ${resp.status}" }}): hay que volver a entrar",
                status = resp.status, isAuthError = authError
            )
        }
        val access = json.optString("access_token", "")
        val refresh = json.optString("refresh_token", "")
        val expiresInSec = json.optLong("expires_in", 0L)
        if (access.isBlank() || refresh.isBlank() || expiresInSec <= 0) {
            throw TidalApiException("Tidal devolvió un token incompleto")
        }
        val session = runCatching {
            TidalApiClient(http, apiBase).getSession(access)
        }.getOrElse { throw TidalApiException("No se pudo cargar la sesión de Tidal: ${it.message}") }
        val tokens = TidalTokens(
            accessToken = access,
            refreshToken = refresh,
            expiresAtMs = System.currentTimeMillis() + expiresInSec * 1_000L,
            userId = session.userId,
            countryCode = session.countryCode,
            sessionId = session.sessionId
        )
        store.save(tokens)
        return tokens
    }

    private fun formBody(vararg pairs: Pair<String, String>): String =
        pairs.joinToString("&") { (k, v) ->
            "${URLEncoder.encode(k, StandardCharsets.UTF_8.name())}=" +
                URLEncoder.encode(v, StandardCharsets.UTF_8.name())
        }
}
