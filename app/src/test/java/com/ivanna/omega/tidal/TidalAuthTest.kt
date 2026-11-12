package com.ivanna.omega.tidal

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

/**
 * TIDAL — tests de autenticación PKCE (v2.5.416).
 * Sin red: el HTTP es falso. El flujo device flow NO se testea porque no
 * se implementa (exige secreto de cliente).
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33])
class TidalAuthTest {

    companion object {
        const val TOKEN_JSON = """{"access_token":"access-1","refresh_token":"refresh-1","expires_in":3600,"token_type":"Bearer"}"""
        const val TOKEN_JSON_2 = """{"access_token":"access-2","refresh_token":"refresh-2","expires_in":3600,"token_type":"Bearer"}"""
        const val SESSIONS_JSON = """{"sessionId":"sess-1","countryCode":"MX","userId":12345}"""
    }

    private fun authWith(
        store: FakeTokenStore = FakeTokenStore(),
        post: (String, String?) -> TidalHttpResponse = { _, _ -> TidalHttpResponse(404, "") },
        get: (String) -> TidalHttpResponse = { TidalHttpResponse(404, "") }
    ): Pair<TidalAuth, FakeHttp> {
        val http = FakeHttp(getHandler = get, postHandler = post)
        return TidalAuth(http, store) to http
    }

    @Test
    fun pkce_rfc7636_testVector() {
        // Vector de prueba del RFC 7636, Apéndice B.
        val verifier = "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"
        assertEquals(
            "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM",
            TidalAuth.Pkce.challenge(verifier)
        )
    }

    @Test
    fun pkce_beginLogin_buildsAuthorizeUrl() {
        val (auth, _) = authWith()
        val session = auth.beginLogin()
        assertTrue(session.authorizeUrl.startsWith("https://login.tidal.com/authorize?"))
        assertTrue(session.authorizeUrl.contains("response_type=code"))
        assertTrue(session.authorizeUrl.contains("client_id=6BDSRdpK9hqEBTgU"))
        assertTrue(session.authorizeUrl.contains("code_challenge_method=S256"))
        assertTrue(session.authorizeUrl.contains("redirect_uri="))
        assertTrue(session.authorizeUrl.contains("restrict_signup=true"))
        // Verifier: 32 bytes → 43 caracteres base64url sin padding.
        assertEquals(43, session.verifier.length)
        assertEquals(
            TidalAuth.Pkce.challenge(session.verifier),
            session.authorizeUrl.substringAfter("code_challenge=").substringBefore("&")
        )
        assertTrue(session.uniqueKey.length == 16)
        assertTrue(auth.state is TidalAuth.AuthState.Authorizing)
    }

    @Test
    fun pkce_extractCodeFromRedirect() {
        assertEquals(
            "abc123",
            TidalAuth.Pkce.extractCodeFromRedirect(
                "https://tidal.com/android/login/auth?code=abc123&state=x"
            )
        )
        assertNull(TidalAuth.Pkce.extractCodeFromRedirect("https://tidal.com/other?code=abc"))
        assertNull(TidalAuth.Pkce.extractCodeFromRedirect("https://tidal.com/android/login/auth?error=access_denied"))
        assertNull(TidalAuth.Pkce.extractCodeFromRedirect("https://tidal.com/android/login/auth"))
    }

    @Test
    fun completeLogin_storesTokensAndSession() {
        val store = FakeTokenStore()
        val (auth, http) = authWith(
            store = store,
            post = { _, body ->
                assertTrue(body!!.contains("grant_type=authorization_code"))
                assertTrue(body.contains("code=CODE-1"))
                assertTrue(body.contains("code_verifier="))
                assertTrue(body.contains("client_unique_key="))
                // SIN secreto: el body jamás debe traer client_secret.
                assertTrue(!body.contains("client_secret"))
                TidalHttpResponse(200, TOKEN_JSON)
            },
            get = { url ->
                assertTrue(url.endsWith("sessions"))
                TidalHttpResponse(200, SESSIONS_JSON)
            }
        )
        val state = auth.completeLogin("CODE-1", "verifier-1", "uniquekey-1")
        assertTrue(state is TidalAuth.AuthState.SignedIn)
        assertEquals(12345L, (state as TidalAuth.AuthState.SignedIn).userId)
        val saved = store.tokens!!
        assertEquals("access-1", saved.accessToken)
        assertEquals("refresh-1", saved.refreshToken)
        assertEquals(12345L, saved.userId)
        assertEquals("MX", saved.countryCode)
        assertTrue(saved.expiresAtMs > System.currentTimeMillis())
        assertEquals(1, http.postedBodies.size)
    }

    @Test
    fun completeLogin_invalidGrant_throwsAuthError() {
        val store = FakeTokenStore()
        val (auth, _) = authWith(
            store = store,
            post = { _, _ -> TidalHttpResponse(400, """{"error":"invalid_grant"}""") }
        )
        try {
            auth.completeLogin("CODE-MALO", "v", "k")
            fail("debió lanzar")
        } catch (e: TidalApiException) {
            assertTrue(e.isAuthError)
        }
        assertNull(store.tokens)
    }

    @Test
    fun getValidAccessToken_fresh_returnsCachedWithoutNetwork() {
        val store = FakeTokenStore()
        store.tokens = sampleTokens(expiresAtMs = System.currentTimeMillis() + 3_600_000L)
        val (auth, http) = authWith(store = store)
        assertEquals("access-1", auth.getValidAccessToken())
        assertTrue(http.requestedUrls.isEmpty())
        assertTrue(http.postedBodies.isEmpty())
    }

    @Test
    fun getValidAccessToken_expired_refreshes() {
        val store = FakeTokenStore()
        store.tokens = sampleTokens(expiresAtMs = System.currentTimeMillis() - 1_000L)
        val (auth, http) = authWith(
            store = store,
            post = { _, body ->
                assertTrue(body!!.contains("grant_type=refresh_token"))
                assertTrue(body.contains("refresh_token=refresh-1"))
                assertTrue(!body.contains("client_secret"))
                TidalHttpResponse(200, TOKEN_JSON_2)
            },
            get = { TidalHttpResponse(200, SESSIONS_JSON) }
        )
        assertEquals("access-2", auth.getValidAccessToken())
        assertEquals("access-2", store.tokens!!.accessToken)
        assertEquals("refresh-2", store.tokens!!.refreshToken)
        assertEquals(1, http.postedBodies.size)
    }

    @Test
    fun getValidAccessToken_refreshInvalidClient_needsRelogin() {
        val store = FakeTokenStore()
        store.tokens = sampleTokens(expiresAtMs = System.currentTimeMillis() - 1_000L)
        val (auth, _) = authWith(
            store = store,
            post = { _, _ -> TidalHttpResponse(400, """{"error":"invalid_client"}""") }
        )
        assertNull(auth.getValidAccessToken())
        assertNull(store.tokens)
        assertTrue(auth.state is TidalAuth.AuthState.NeedsRelogin)
    }

    @Test
    fun getValidAccessToken_refreshInvalidGrant_needsRelogin() {
        val store = FakeTokenStore()
        store.tokens = sampleTokens(expiresAtMs = System.currentTimeMillis() - 1_000L)
        val (auth, _) = authWith(
            store = store,
            post = { _, _ -> TidalHttpResponse(400, """{"error":"invalid_grant"}""") }
        )
        assertNull(auth.getValidAccessToken())
        assertNull(store.tokens)
        assertTrue(auth.state is TidalAuth.AuthState.NeedsRelogin)
    }

    @Test
    fun signOut_clearsStoreAndState() {
        val store = FakeTokenStore()
        store.tokens = sampleTokens()
        val (auth, _) = authWith(store = store)
        auth.signOut()
        assertNull(store.tokens)
        assertTrue(auth.state is TidalAuth.AuthState.SignedOut)
    }
}
