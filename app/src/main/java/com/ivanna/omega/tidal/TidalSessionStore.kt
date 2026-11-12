package com.ivanna.omega.tidal

import android.content.Context
import android.content.SharedPreferences
import android.util.Log
import androidx.security.crypto.EncryptedSharedPreferences
import androidx.security.crypto.MasterKey

/**
 * TIDAL — almacén de tokens (v2.5.416).
 *
 * SOLO tokens (access/refresh/expiración/usuario). La contraseña de Tidal
 * NUNCA se guarda: Esse la escribe en la página de Tidal (WebView) y la app
 * jamás la ve. Los tokens nunca se loguean (ni logcat, ni archivos).
 *
 * Almacén real: EncryptedSharedPreferences "ivanna_tidal" (mismo patrón que
 * SecureConfigurationManager: MasterKey AES256_GCM, claves AES256_SIV,
 * valores AES256_GCM). Si el cifrado no está disponible en el dispositivo,
 * la sesión vive SOLO en memoria (nunca fallback en claro) y la UI lo
 * advierte: al cerrar la app hay que volver a entrar.
 */

data class TidalTokens(
    val accessToken: String,
    val refreshToken: String,
    /** Epoch ms en que expira el access token. */
    val expiresAtMs: Long,
    val userId: Long,
    val countryCode: String,
    val sessionId: String
) {
    fun isExpired(nowMs: Long = System.currentTimeMillis()): Boolean = nowMs >= expiresAtMs
}

interface TidalTokenStore {
    fun save(tokens: TidalTokens)
    fun load(): TidalTokens?
    fun clear()
    /** false = el cifrado no está disponible; la sesión es solo en memoria. */
    val encryptionAvailable: Boolean
}

class TidalSessionStore(context: Context) : TidalTokenStore {

    companion object {
        private const val TAG = "IVANNA.TidalStore"
        private const val PREFS_NAME = "ivanna_tidal"
        private const val KEY_ACCESS = "access_token"
        private const val KEY_REFRESH = "refresh_token"
        private const val KEY_EXPIRES = "expires_at_ms"
        private const val KEY_USER_ID = "user_id"
        private const val KEY_COUNTRY = "country_code"
        private const val KEY_SESSION = "session_id"
    }

    private val appContext = context.applicationContext
    private var memoryOnly: TidalTokens? = null

    private val encryptedPrefs: SharedPreferences? = runCatching {
        val masterKey = MasterKey.Builder(appContext, "ivanna_tidal_master")
            .setKeyScheme(MasterKey.KeyScheme.AES256_GCM)
            .setUserAuthenticationRequired(false)
            .build()
        EncryptedSharedPreferences.create(
            appContext, PREFS_NAME, masterKey,
            EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV,
            EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM
        )
    }.onFailure {
        Log.w(TAG, "Cifrado no disponible, sesión solo en memoria: ${it.message}")
    }.getOrNull()

    override val encryptionAvailable: Boolean get() = encryptedPrefs != null

    override fun save(tokens: TidalTokens) {
        memoryOnly = tokens
        encryptedPrefs?.edit()
            ?.putString(KEY_ACCESS, tokens.accessToken)
            ?.putString(KEY_REFRESH, tokens.refreshToken)
            ?.putLong(KEY_EXPIRES, tokens.expiresAtMs)
            ?.putLong(KEY_USER_ID, tokens.userId)
            ?.putString(KEY_COUNTRY, tokens.countryCode)
            ?.putString(KEY_SESSION, tokens.sessionId)
            ?.apply()
    }

    override fun load(): TidalTokens? {
        val prefs = encryptedPrefs
        if (prefs != null) {
            val access = prefs.getString(KEY_ACCESS, null) ?: return memoryOnly
            val refresh = prefs.getString(KEY_REFRESH, null) ?: return memoryOnly
            return TidalTokens(
                accessToken = access,
                refreshToken = refresh,
                expiresAtMs = prefs.getLong(KEY_EXPIRES, 0L),
                userId = prefs.getLong(KEY_USER_ID, -1L),
                countryCode = prefs.getString(KEY_COUNTRY, "") ?: "",
                sessionId = prefs.getString(KEY_SESSION, "") ?: ""
            )
        }
        return memoryOnly
    }

    override fun clear() {
        memoryOnly = null
        encryptedPrefs?.edit()?.clear()?.apply()
    }
}
