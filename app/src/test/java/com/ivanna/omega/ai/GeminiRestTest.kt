package com.ivanna.omega.ai

import com.ivanna.omega.ai.gemini.GeminiRest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class GeminiRestTest {
    @Test fun cleanKeyRemovesPasteArtifacts() {
        assertEquals("AIzaSyABC123", GeminiRest.cleanKey("  \"AIzaSyABC123\"\n"))
        assertEquals("AQ.abcdef", GeminiRest.cleanKey("AQ.abc\r\ndef "))
    }

    @Test fun androidRestrictedKeyIsExplained() {
        val body = """{"error":{"code":403,"message":"Requests from this Android client application <empty> are blocked.",
            "status":"PERMISSION_DENIED","details":[{"reason":"API_KEY_ANDROID_APP_BLOCKED"}]}}"""
        val e = GeminiRest.parseError(403, body)
        assertEquals("API_KEY_ANDROID_APP_BLOCKED", e.reason)
        assertTrue(GeminiRest.explain(e).contains("restringida a otra app Android"))
    }

    @Test fun disabledApiAndLeakedKeyAreDistinguished() {
        val disabled = GeminiRest.parseError(403,
            """{"error":{"message":"Generative Language API has not been used in project 1 before","status":"PERMISSION_DENIED"}}""")
        assertTrue(GeminiRest.explain(disabled).contains("no está habilitada"))
        val leaked = GeminiRest.parseError(403,
            """{"error":{"message":"Your API key was reported as leaked. Please use another API key.","status":"PERMISSION_DENIED"}}""")
        assertTrue(GeminiRest.explain(leaked).contains("filtrada"))
    }

    @Test fun bareStatusStillGivesACause() {
        assertTrue(GeminiRest.explain(GeminiRest.parseError(403, "")).startsWith("HTTP 403"))
        assertTrue(GeminiRest.explain(GeminiRest.parseError(429, "{}")).contains("cuota"))
    }
}
