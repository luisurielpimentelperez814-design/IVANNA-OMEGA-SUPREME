package com.ivanna.omega.ai

import com.ivanna.omega.ai.gemini.OmniRouteOrchestrator
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/**
 * Tests del OmniRouteOrchestrator (v2.5.443).
 *
 * Solo funciones puras + transición del circuit breaker (sin red): el
 * camino vivo se verifica contra el gateway real de la VM por curl
 * (GET /v1/models y POST /v1/chat/completions con Pollinations).
 *
 * Robolectric: org.json.JSONObject es un stub en el android.jar de los
 * unit tests (sin él, buildChatRequest da NPE y parseChatContent/
 * parseSseDelta devuelven null). Igual que TidalApiParsingTest.
 */
@RunWith(RobolectricTestRunner::class)
class OmniRouteOrchestratorTest {

    // ── buildChatRequest ─────────────────────────────────────────────────

    @Test fun chatRequestIncludesSystemUserAndModel() {
        val json = OmniRouteOrchestrator.buildChatRequest(
            model = "ivanna-free",
            systemInstruction = "Eres IVANNA.",
            prompt = "Hola",
            temperature = 0.7f,
            maxTokens = 1024,
            stream = false
        )
        assertTrue(json.contains("\"model\":\"ivanna-free\""))
        assertTrue(json.contains("\"role\":\"system\""))
        assertTrue(json.contains("Eres IVANNA."))
        assertTrue(json.contains("\"role\":\"user\""))
        assertTrue(json.contains("\"content\":\"Hola\""))
        assertTrue(json.contains("\"stream\":false"))
        assertTrue(json.contains("\"max_tokens\":1024"))
    }

    @Test fun chatRequestOmitsSystemWhenNull() {
        val json = OmniRouteOrchestrator.buildChatRequest(
            model = "pol/claude",
            systemInstruction = null,
            prompt = "Hola",
            temperature = 0.3f,
            maxTokens = 256,
            stream = true
        )
        assertFalse(json.contains("\"role\":\"system\""))
        assertTrue(json.contains("\"stream\":true"))
        assertTrue(json.contains("\"max_tokens\":256"))
    }

    // ── parseChatContent ─────────────────────────────────────────────────

    @Test fun chatContentExtractsFirstChoice() {
        val body = """{"id":"chatcmpl-1","choices":[{"index":0,"message":{"role":"assistant","content":"Hola, soy IVANNA."},"finish_reason":"stop"}]}"""
        assertEquals("Hola, soy IVANNA.", OmniRouteOrchestrator.parseChatContent(body))
    }

    @Test fun chatContentNullWhenNoChoices() {
        assertNull(OmniRouteOrchestrator.parseChatContent("""{"choices":[]}"""))
        assertNull(OmniRouteOrchestrator.parseChatContent("""{}"""))
        assertNull(OmniRouteOrchestrator.parseChatContent("no-json"))
    }

    @Test fun chatContentFallsBackToSseBody() {
        // Algunos proveedores del gateway devuelven SSE aunque no se pida
        // stream: el contenido se reconstruye concatenando los deltas.
        val sseBody = "data: {\"id\":\"chatcmpl-keepalive\",\"object\":\"chat.completion.chunk\",\"choices\":[]}\n\n" +
            "data: {\"choices\":[{\"delta\":{\"content\":\"2\"}}]}\n\n" +
            "data: {\"choices\":[{\"delta\":{\"content\":\"+2 es 4\"}}]}\n\n" +
            "data: [DONE]\n"
        assertEquals("2+2 es 4", OmniRouteOrchestrator.parseChatContent(sseBody))
    }

    // ── SSE ──────────────────────────────────────────────────────────────

    @Test fun sseDeltaExtractsContent() {
        val line = """data: {"choices":[{"delta":{"content":"Hola"}}]}"""
        assertEquals("Hola", OmniRouteOrchestrator.parseSseDelta(line))
    }

    @Test fun sseDeltaNullForDoneEmptyAndComments() {
        assertNull(OmniRouteOrchestrator.parseSseDelta("data: [DONE]"))
        assertNull(OmniRouteOrchestrator.parseSseDelta("data:[DONE]"))
        assertNull(OmniRouteOrchestrator.parseSseDelta(""))
        assertNull(OmniRouteOrchestrator.parseSseDelta(": ping"))
        assertNull(OmniRouteOrchestrator.parseSseDelta("data: {malformed"))
        // Chunk sin contenido (p. ej. solo role) no es texto.
        assertNull(OmniRouteOrchestrator.parseSseDelta("""data: {"choices":[{"delta":{"role":"assistant"}}]}"""))
    }

    @Test fun isSseDoneDetectsTerminator() {
        assertTrue(OmniRouteOrchestrator.isSseDone("data: [DONE]"))
        assertTrue(OmniRouteOrchestrator.isSseDone("data:[DONE]"))
        assertFalse(OmniRouteOrchestrator.isSseDone("""data: {"choices":[]}"""))
        assertFalse(OmniRouteOrchestrator.isSseDone(""))
    }

    // ── Errores ──────────────────────────────────────────────────────────

    @Test fun error401IsKeyAnd429IsQuota() {
        val unauth = OmniRouteOrchestrator.parseError(401, """{"error":{"message":"Incorrect API key","type":"invalid_request_error","code":"invalid_api_key"}}""")
        assertTrue(OmniRouteOrchestrator.explainError(unauth).contains("API key"))
        val quota = OmniRouteOrchestrator.parseError(429, """{"error":{"message":"Rate limit","type":"rate_limit_error","code":"rate_limit_exceeded"}}""")
        assertTrue(OmniRouteOrchestrator.explainError(quota).contains("cuota"))
    }

    @Test fun error404IsModelAnd502IsGateway() {
        val notFound = OmniRouteOrchestrator.parseError(404, """{"error":{"message":"model not found","type":"invalid_request_error","code":"model_not_found"}}""")
        assertTrue(OmniRouteOrchestrator.explainError(notFound).contains("Modelo"))
        val badGateway = OmniRouteOrchestrator.parseError(502, """{"error":{"message":"upstream down","type":"server_error"}}""")
        assertTrue(OmniRouteOrchestrator.explainError(badGateway).contains("gateway"))
    }

    @Test fun bareErrorBodyStillExplains() {
        val e = OmniRouteOrchestrator.parseError(503, "")
        assertTrue(OmniRouteOrchestrator.explainError(e).contains("503"))
    }

    // ── Circuit breaker (transición pura) ────────────────────────────────

    private fun health() = OmniRouteOrchestrator.ModelHealth(modelName = "ivanna-free")

    @Test fun circuitStaysClosedBelowThreshold() {
        var h = health()
        h = OmniRouteOrchestrator.transitionHealth(h, success = false, latencyMs = 100, nowMs = 1000)
        assertEquals(1, h.consecutiveFailures)
        assertEquals(OmniRouteOrchestrator.ModelHealth.CircuitState.CLOSED, h.circuitState)
        h = OmniRouteOrchestrator.transitionHealth(h, success = false, latencyMs = 100, nowMs = 2000)
        assertEquals(2, h.consecutiveFailures)
        assertEquals(OmniRouteOrchestrator.ModelHealth.CircuitState.CLOSED, h.circuitState)
        assertTrue(h.isHealthy)
    }

    @Test fun circuitOpensAtThreeFailures() {
        var h = health()
        repeat(3) { i ->
            h = OmniRouteOrchestrator.transitionHealth(h, success = false, latencyMs = 100, nowMs = 1000L + i)
        }
        assertEquals(3, h.consecutiveFailures)
        assertEquals(OmniRouteOrchestrator.ModelHealth.CircuitState.OPEN, h.circuitState)
        assertFalse(h.isHealthy)
        assertEquals(3, h.totalFailures)
    }

    @Test fun successResetsFailuresAndClosesCircuit() {
        var h = health()
        repeat(2) { i ->
            h = OmniRouteOrchestrator.transitionHealth(h, success = false, latencyMs = 100, nowMs = 1000L + i)
        }
        h = OmniRouteOrchestrator.transitionHealth(h, success = true, latencyMs = 250, nowMs = 3000)
        assertEquals(0, h.consecutiveFailures)
        assertEquals(OmniRouteOrchestrator.ModelHealth.CircuitState.CLOSED, h.circuitState)
        assertTrue(h.isHealthy)
        assertEquals(250, h.averageLatencyMs)
    }
}
