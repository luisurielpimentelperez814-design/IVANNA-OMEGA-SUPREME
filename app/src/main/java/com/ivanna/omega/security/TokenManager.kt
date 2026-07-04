package com.ivanna.omega.security

import android.os.Handler
import android.os.Looper
import android.util.Log
import java.util.UUID
import java.util.concurrent.ConcurrentHashMap

/**
 * TokenManager
 *
 * Gestiona credenciales de corta duración usadas internamente por IVANNA-OMEGA-SUPREME
 * (handshakes con el daemon Magisk, sesiones de PermissionManager, etc).
 *
 * No almacena ni transporta tokens externos (GitHub, APIs de terceros) — esto es
 * exclusivamente para tokens internos de la app con auto-revocación por TTL.
 *
 * No rompe nada existente: es un módulo nuevo, independiente.
 */
class TokenManager {

    enum class TokenType {
        PERMISSION_ACCESS,
        AUDIO_PROCESSING,
        MAGISK_HOOK
    }

    private data class TokenRecord(
        val id: String,
        val type: TokenType,
        val createdAt: Long,
        val expiresAt: Long,
        val event: String
    )

    private val tokens = ConcurrentHashMap<String, TokenRecord>()
    private val mainHandler = Handler(Looper.getMainLooper())

    fun generateToken(
        type: TokenType,
        event: String,
        durationSeconds: Long = 3600L
    ): String {
        val tokenId = UUID.randomUUID().toString()
        val now = System.currentTimeMillis()
        tokens[tokenId] = TokenRecord(
            id = tokenId,
            type = type,
            createdAt = now,
            expiresAt = now + durationSeconds * 1000L,
            event = event
        )
        Log.d(TAG, "Token generado: type=$type event=$event ttl=${durationSeconds}s")
        scheduleAutoRevoke(tokenId, durationSeconds)
        return tokenId
    }

    fun revokeToken(tokenId: String) {
        if (tokens.remove(tokenId) != null) {
            Log.i(TAG, "Token revocado: $tokenId")
        }
    }

    fun revokeAllExpired() {
        val now = System.currentTimeMillis()
        val expired = tokens.filterValues { it.expiresAt < now }.keys
        expired.forEach { tokens.remove(it) }
    }

    fun revokeAll() {
        val n = tokens.size
        tokens.clear()
        if (n > 0) Log.i(TAG, "Revocados $n tokens (revokeAll)")
    }

    fun isTokenValid(tokenId: String): Boolean =
        tokens[tokenId]?.let { it.expiresAt > System.currentTimeMillis() } ?: false

    fun getActiveTokenCount(): Int = tokens.size

    private fun scheduleAutoRevoke(tokenId: String, delaySeconds: Long) {
        mainHandler.postDelayed({ revokeToken(tokenId) }, delaySeconds * 1000L)
    }

    companion object {
        private const val TAG = "TokenManager"

        @Volatile
        private var INSTANCE: TokenManager? = null

        fun getInstance(): TokenManager =
            INSTANCE ?: synchronized(this) {
                INSTANCE ?: TokenManager().also { INSTANCE = it }
            }
    }
}
