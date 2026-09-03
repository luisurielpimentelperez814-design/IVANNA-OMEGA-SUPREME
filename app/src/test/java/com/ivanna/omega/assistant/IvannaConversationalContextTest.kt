package com.ivanna.omega.assistant

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

/**
 * Verifica que la cadena real de contexto conversacional está conectada:
 *
 *   texto del usuario → updateTemporalPreferences() → estado de sesión
 *     → preferencesSummary() → contextSummary() (lo que consume
 *       IvannaGeminiAgent.buildSystemPrompt())
 */
class IvannaConversationalContextTest {

    @Before
    fun reset() = IvannaConversationalCore.clear()

    @Test
    fun `las preferencias habladas se escriben en el estado de sesion`() {
        IvannaConversationalCore.updateTemporalPreferences("no me gustan los bajos muy fuertes")
        val prefs = IvannaConversationalCore.context.value.temporalPreferences
        assertEquals(IvannaConversationalCore.BassPreference.LOW, prefs.bassPreference)
    }

    @Test
    fun `las preferencias activas aparecen en el resumen de contexto`() {
        IvannaConversationalCore.updateTemporalPreferences("prefiero escuchar suave")
        IvannaConversationalCore.updateTemporalPreferences("sin reverb")
        val summary = IvannaConversationalCore.contextSummary()
        assertTrue("El resumen debe exponer el volumen suave: $summary",
            summary.contains("volumen suave"))
        assertTrue("El resumen debe exponer la preferencia seca: $summary",
            summary.contains("poca reverberación"))
    }

    @Test
    fun `sin preferencias expresadas el resumen de preferencias es vacio`() {
        assertEquals("", IvannaConversationalCore.preferencesSummary())
    }

    @Test
    fun `la memoria de sesion influye en el contexto de turnos posteriores`() {
        IvannaConversationalCore.updateTemporalPreferences("quiero más graves")
        IvannaConversationalCore.recordTurn("pon música", "MUSIC_FULLNESS", "MUSIC", "Listo")
        IvannaConversationalCore.recordTurn("y hazla más épica", "MUSICAL_INTENT", "ÉPICO", "Hecho")
        val summary = IvannaConversationalCore.contextSummary()
        assertTrue("Debe conservar la preferencia del turno anterior: $summary",
            summary.contains("prefiere más graves"))
        assertTrue("Debe conservar el último preset: $summary", summary.contains("ÉPICO"))
        assertTrue("Debe conservar el historial de intenciones: $summary",
            summary.contains("MUSICAL_INTENT"))
    }

    @Test
    fun `clear borra las preferencias temporales`() {
        IvannaConversationalCore.updateTemporalPreferences("más brillante")
        IvannaConversationalCore.clear()
        assertEquals(
            IvannaConversationalCore.WarmthPreference.NEUTRAL,
            IvannaConversationalCore.context.value.temporalPreferences.warmthPreference
        )
    }
}
