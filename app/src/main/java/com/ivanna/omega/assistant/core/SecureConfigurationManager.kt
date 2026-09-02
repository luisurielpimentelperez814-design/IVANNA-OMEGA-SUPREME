package com.ivanna.omega.assistant.core

import android.content.Context
import android.content.SharedPreferences
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import android.util.Log
import androidx.security.crypto.EncryptedSharedPreferences
import androidx.security.crypto.MasterKey
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

object SecureConfigurationManager {
    private const val TAG = "SecureConfigMgr"
    private const val PREFS_FILENAME = "ivanna_secure_v2"
    private const val KEY_ALIAS = "ivanna_master_key_v2"
    private const val ANDROID_KEYSTORE = "AndroidKeyStore"
    private const val KEY_SIZE = 256
    private const val FALLBACK_PREFS = "ivanna_fallback_v2"
    private const val K_IV = "iv"
    private const val K_CIPHER_TEXT = "ct"
    private const val TRANSFORMATION = "AES/GCM/NoPadding"
    private const val GCM_TAG_BITS = 128
    private const val KEY_GEMINI_API = "gemini_api_key"

    private var appContext: Context? = null
    private var securePrefs: SharedPreferences? = null
    private var isUsingFallback = false

    private val _state = MutableStateFlow(State())
    val state: StateFlow<State> = _state.asStateFlow()

    data class State(val isInitialized: Boolean = false, val isConfigured: Boolean = false, val isHealthy: Boolean = false, val isUsingFallback: Boolean = false, val lastError: String? = null)

    @Synchronized
    fun initialize(context: Context) {
        if (_state.value.isInitialized) return
        appContext = context.applicationContext
        runCatching {
            securePrefs = createEncryptedPrefs(appContext!!)
            validateConfiguration()
        }.onSuccess {
            _state.value = State(isInitialized = true, isHealthy = true)
            Log.i(TAG, "Initialized with EncryptedSharedPreferences")
        }.onFailure { error ->
            Log.e(TAG, "Primary init failed: ${error.message}")
            recoverFromFailure(error)
        }
    }

    fun setApiKey(key: String) {
        requireInitialized()
        if (!isValidKeyFormat(key)) { Log.w(TAG, "Invalid API key format rejected"); return }
        if (isUsingFallback) saveEncryptedFallback(KEY_GEMINI_API, key)
        else securePrefs?.edit()?.putString(KEY_GEMINI_API, key)?.apply()
        validateConfiguration()
        Log.i(TAG, "API key stored securely")
    }

    fun getApiKey(): String {
        requireInitialized()
        return if (isUsingFallback) loadEncryptedFallback(KEY_GEMINI_API) ?: "" else securePrefs?.getString(KEY_GEMINI_API, "") ?: ""
    }

    fun clearApiKey() {
        requireInitialized()
        if (isUsingFallback) getFallbackPrefs().edit().remove(KEY_GEMINI_API).apply()
        else securePrefs?.edit()?.remove(KEY_GEMINI_API)?.apply()
        validateConfiguration()
    }

    fun hasValidApiKey(): Boolean {
        val key = getApiKey()
        return key.isNotBlank() && isValidKeyFormat(key)
    }

    fun rotateMasterKey(): Boolean {
        requireInitialized()
        if (isUsingFallback) { Log.w(TAG, "Cannot rotate key while using fallback"); return false }
        return runCatching {
            val currentKey = getApiKey()
            val ks = KeyStore.getInstance(ANDROID_KEYSTORE).apply { load(null) }
            ks.deleteEntry(KEY_ALIAS)
            securePrefs = createEncryptedPrefs(appContext!!)
            if (currentKey.isNotBlank()) securePrefs?.edit()?.putString(KEY_GEMINI_API, currentKey)?.apply()
            _state.value = _state.value.copy(isUsingFallback = false)
            Log.i(TAG, "Master key rotated successfully")
            true
        }.getOrElse { Log.e(TAG, "Key rotation failed: ${it.message}"); false }
    }

    private fun createEncryptedPrefs(context: Context): SharedPreferences {
        val masterKey = MasterKey.Builder(context, KEY_ALIAS).setKeyScheme(MasterKey.KeyScheme.AES256_GCM).setUserAuthenticationRequired(false).build()
        return EncryptedSharedPreferences.create(context, PREFS_FILENAME, masterKey, EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV, EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM)
    }

    private fun recoverFromFailure(error: Throwable) {
        Log.w(TAG, "Attempting recovery...")
        runCatching {
            context().getSharedPreferences(PREFS_FILENAME, Context.MODE_PRIVATE).edit().clear().apply()
            securePrefs = createEncryptedPrefs(context())
            validateConfiguration()
        }.onSuccess {
            _state.value = State(isInitialized = true, isHealthy = true)
            Log.i(TAG, "Recovered by clearing corrupted prefs")
            return
        }
        Log.w(TAG, "Using manual Keystore fallback")
        isUsingFallback = true
        _state.value = State(isInitialized = true, isHealthy = true, isUsingFallback = true, lastError = "Fallback: ${error.message}")
    }

    private fun saveEncryptedFallback(key: String, value: String) {
        val secretKey = getOrCreateFallbackKey() ?: return
        val cipher = Cipher.getInstance(TRANSFORMATION).apply { init(Cipher.ENCRYPT_MODE, secretKey) }
        getFallbackPrefs().edit()
            .putString("$key.$K_IV", Base64.encodeToString(cipher.iv, Base64.NO_WRAP))
            .putString("$key.$K_CIPHER_TEXT", Base64.encodeToString(cipher.doFinal(value.toByteArray(Charsets.UTF_8)), Base64.NO_WRAP))
            .apply()
    }

    private fun loadEncryptedFallback(key: String): String? {
        val secretKey = getOrCreateFallbackKey() ?: return null
        val prefs = getFallbackPrefs()
        val ivB64 = prefs.getString("$key.$K_IV", null) ?: return null
        val ctB64 = prefs.getString("$key.$K_CIPHER_TEXT", null) ?: return null
        val iv = Base64.decode(ivB64, Base64.NO_WRAP)
        val ciphertext = Base64.decode(ctB64, Base64.NO_WRAP)
        val cipher = Cipher.getInstance(TRANSFORMATION).apply { init(Cipher.DECRYPT_MODE, secretKey, GCMParameterSpec(GCM_TAG_BITS, iv)) }
        return String(cipher.doFinal(ciphertext), Charsets.UTF_8)
    }

    private fun getOrCreateFallbackKey(): SecretKey? {
        return runCatching {
            val ks = KeyStore.getInstance(ANDROID_KEYSTORE).apply { load(null) }
            (ks.getEntry(KEY_ALIAS, null) as? KeyStore.SecretKeyEntry)?.secretKey ?: run {
                val kg = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, ANDROID_KEYSTORE)
                kg.init(KeyGenParameterSpec.Builder(KEY_ALIAS, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                    .setBlockModes(KeyProperties.BLOCK_MODE_GCM).setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                    .setKeySize(KEY_SIZE).setRandomizedEncryptionRequired(true).build())
                kg.generateKey()
            }
        }.getOrElse { Log.e(TAG, "Fallback key error: ${it.message}"); null }
    }

    private fun getFallbackPrefs(): SharedPreferences = context().getSharedPreferences(FALLBACK_PREFS, Context.MODE_PRIVATE)
    private fun validateConfiguration() { val key = getApiKey(); _state.value = _state.value.copy(isConfigured = key.isNotBlank() && isValidKeyFormat(key)) }
    private fun isValidKeyFormat(key: String): Boolean = key.startsWith("AIza") && key.length >= 20
    private fun requireInitialized() { check(_state.value.isInitialized) { "Call initialize() first" } }
    private fun context(): Context = appContext ?: throw IllegalStateException("No context")
}
