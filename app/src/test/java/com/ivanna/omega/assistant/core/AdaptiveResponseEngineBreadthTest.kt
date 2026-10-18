package com.ivanna.omega.assistant.core

import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33])
class AdaptiveResponseEngineBreadthTest {

    @Test
    fun generalKnowledgeExplanationUsesDeepReasoning() {
        val profile = AdaptiveResponseEngine.analyzeComplexity(
            "Cuéntame sobre la historia de la astronomía y desarrolla el contexto histórico"
        )
        assertEquals(AdaptiveResponseEngine.ResponseProfile.DEEP_REASONING, profile)
    }

    @Test
    fun musicalAnalysisUsesDeepReasoning() {
        val profile = AdaptiveResponseEngine.analyzeComplexity(
            "Analiza la producción musical, la armonía y la mezcla de esta grabación"
        )
        assertEquals(AdaptiveResponseEngine.ResponseProfile.DEEP_REASONING, profile)
    }

    @Test
    fun simpleGreetingRemainsFast() {
        val profile = AdaptiveResponseEngine.analyzeComplexity("Hola")
        assertEquals(AdaptiveResponseEngine.ResponseProfile.FAST, profile)
    }
}
