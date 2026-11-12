package com.ivanna.omega.tidal

/**
 * Fakes compartidos por los tests de TIDAL (v2.5.416).
 * Ninguno toca la red: la API de Tidal nunca se martilla en CI.
 */

class FakeHttp(
    var getHandler: (String) -> TidalHttpResponse = { TidalHttpResponse(404, "") },
    var postHandler: (String, String?) -> TidalHttpResponse = { _, _ -> TidalHttpResponse(404, "") },
    var bytesHandler: (String) -> ByteArray = { ByteArray(0) }
) : TidalHttp {
    val requestedUrls = mutableListOf<String>()
    val postedBodies = mutableListOf<String>()

    override fun request(
        method: String,
        url: String,
        headers: Map<String, String>,
        body: String?
    ): TidalHttpResponse {
        requestedUrls.add(url)
        if (method == "POST") postedBodies.add(body ?: "")
        return if (method == "POST") postHandler(url, body) else getHandler(url)
    }

    override fun requestBytes(url: String, headers: Map<String, String>): ByteArray {
        requestedUrls.add(url)
        return bytesHandler(url)
    }
}

class FakeTokenStore : TidalTokenStore {
    var tokens: TidalTokens? = null
    override fun save(tokens: TidalTokens) { this.tokens = tokens }
    override fun load(): TidalTokens? = tokens
    override fun clear() { tokens = null }
    override val encryptionAvailable: Boolean = true
}

fun sampleTokens(
    access: String = "access-1",
    refresh: String = "refresh-1",
    expiresAtMs: Long = System.currentTimeMillis() + 3_600_000L,
    userId: Long = 12345L
) = TidalTokens(
    accessToken = access,
    refreshToken = refresh,
    expiresAtMs = expiresAtMs,
    userId = userId,
    countryCode = "MX",
    sessionId = "sess-1"
)
