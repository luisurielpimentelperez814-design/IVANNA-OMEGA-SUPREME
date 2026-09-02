package com.ivanna.omega.core

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/**
 * GeminiKeyStore — almacenamiento cifrado de la GEMINI_API_KEY.
 *
 * Por qué existe: la key del usuario se persistía en SharedPreferences PLANO
 * (audit FASE 1: app sin androidx.security:security-crypto, ViewModel escribía
 * el string tal cual). Cualquier app root / backup ADB la leía en claro.
 *
 * Diseño: AES-256/GCM con clave generada y custodiada por el Android Keystore
 * (TEE/StrongBox cuando el hardware lo soporta — Moto G85 incluido). La clave
 * NUNCA sale del hardware; solo el ciphertext viaja a disco.
 *
 * API mínima: save / load / clear / isPresent. Fallback seguro: si el Keystore
 * no está disponible (edge cases de OEM), NO se degrada a plano — devuelve
 * null y el caller mantiene el comportamiento actual (BuildConfig o prompt).
 */
object GeminiKeyStore {
    private const val ANDROID_KEYSTORE = "AndroidKeyStore"
    private const val KEY_ALIAS        = "ivanna_gemini_api_key"
    private const val PREFS            = "ivanna_gemini_keystore"
    private const val K_CIPHERTEXT     = "ct"
    private const val K_IV             = "iv"
    private const val TRANSFORMATION   = "AES/GCM/NoPadding"
    private const val GCM_TAG_BITS     = 128

    private fun secretKey(): SecretKey? = runCatching {
        val ks = KeyStore.getInstance(ANDROID_KEYSTORE).apply { load(null) }
        (ks.getEntry(KEY_ALIAS, null) as? KeyStore.SecretKeyEntry)?.secretKey
            ?: run {
                val kg = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, ANDROID_KEYSTORE)
                kg.init(
                    KeyGenParameterSpec.Builder(
                        KEY_ALIAS,
                        KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT
                    )
                        .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                        .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                        .setKeySize(256)
                        .build()
                )
                kg.generateKey()
            }
    }.getOrNull()

    /** Persiste la API key cifrada. Devuelve true si quedó protegida por Keystore. */
    fun save(context: Context, apiKey: String): Boolean {
        val key = secretKey() ?: return false
        return runCatching {
            val cipher = Cipher.getInstance(TRANSFORMATION)
            cipher.init(Cipher.ENCRYPT_MODE, key)
            val iv = cipher.iv
            val ct = cipher.doFinal(apiKey.toByteArray(Charsets.UTF_8))
            context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                .putString(K_CIPHERTEXT, Base64.encodeToString(ct, Base64.NO_WRAP))
                .putString(K_IV, Base64.encodeToString(iv, Base64.NO_WRAP))
                .apply()
            true
        }.getOrDefault(false)
    }

    /** Recupera la API key en claro (solo en memoria del proceso). null si no hay o falla. */
    fun load(context: Context): String? {
        val key = secretKey() ?: return null
        val p = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        val ctB64 = p.getString(K_CIPHERTEXT, null) ?: return null
        val ivB64 = p.getString(K_IV, null) ?: return null
        return runCatching {
            val cipher = Cipher.getInstance(TRANSFORMATION)
            cipher.init(Cipher.DECRYPT_MODE, key, GCMParameterSpec(GCM_TAG_BITS, Base64.decode(ivB64, Base64.NO_WRAP)))
            String(cipher.doFinal(Base64.decode(ctB64, Base64.NO_WRAP)), Charsets.UTF_8)
        }.getOrNull()
    }

    fun isPresent(context: Context): Boolean =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).contains(K_CIPHERTEXT)

    fun clear(context: Context) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().clear().apply()
        runCatching {
            KeyStore.getInstance(ANDROID_KEYSTORE).apply { load(null) }.deleteEntry(KEY_ALIAS)
        }
    }
}
