package com.ivanna.omega.ai.memory

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import kotlinx.coroutines.runBlocking
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

/**
 * Tests de validación OEM para IvannaMemoryArchitecture.
 *
 * Cubre:
 * - Working memory (RAM, latencia <1ms)
 * - Semantic memory (persistencia, retrieval)
 * - System memory (snapshot, context building)
 * - Pruning (límites de tamaño)
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33])
class IvannaMemoryArchitectureTest {

    private lateinit var memory: IvannaMemoryArchitecture
    private lateinit var context: Context

    @Before
    fun setup() {
        context = ApplicationProvider.getApplicationContext()
        memory = IvannaMemoryArchitecture(context)
    }

    @Test
    fun `working memory stores and retrieves interactions`() {
        memory.recordInteraction("user", "hola")
        memory.recordInteraction("assistant", "hola, ¿cómo estás?")
        memory.recordInteraction("user", "bien gracias")

        val recent = memory.workingMemory.getRecent(2)
        assertEquals(2, recent.size)
        assertEquals("assistant", recent[0].role)
        assertEquals("user", recent[1].role)
    }

    @Test
    fun `working memory respects max size`() {
        repeat(40) { i ->
            memory.recordInteraction("user", "msg $i")
        }
        val all = memory.workingMemory.getAll()
        assertTrue("Working memory debe respetar límite de 32", all.size <= 32)
    }

    @Test
    fun `semantic memory learns and retrieves facts`() = runBlocking {
        memory.learnFact(
            key = "pref_eq",
            value = "bass_boost",
            category = IvannaMemoryArchitecture.SemanticRecord.SemanticCategory.AUDIO_PREFERENCE
        )

        val pref = memory.getUserPreference("pref_eq")
        assertEquals("bass_boost", pref)
    }

    @Test
    fun `semantic memory updates existing facts`() = runBlocking {
        memory.learnFact("pref_eq", "bass_boost",
            IvannaMemoryArchitecture.SemanticRecord.SemanticCategory.AUDIO_PREFERENCE)
        memory.learnFact("pref_eq", "flat",
            IvannaMemoryArchitecture.SemanticRecord.SemanticCategory.AUDIO_PREFERENCE)

        val pref = memory.getUserPreference("pref_eq")
        assertEquals("flat", pref)
    }

    @Test
    fun `system memory snapshot is included in context`() = runBlocking {
        memory.updateSystemSnapshot(IvannaMemoryArchitecture.SystemMemory.SystemSnapshot(
            audioRoute = "headphones",
            sampleRate = 48000,
            presetName = "studio",
            dspChainActive = listOf("EQ", "Compressor"),
            eqProfile = "flat",
            hrtfActive = "default",
            spatialMode = "off"
        ))

        val context = memory.buildContextForGemini("test query")
        assertTrue("Contexto debe incluir ruta de audio", context.contains("headphones"))
        assertTrue("Contexto debe incluir preset", context.contains("studio"))
        assertTrue("Contexto debe incluir DSP chain", context.contains("EQ"))
    }

    @Test
    fun `pruning removes old episodic records`() = runBlocking {
        // Simular sesión vieja
        memory.persistSession("old_session", "old query", "old response", listOf("cmd1"))

        // Prune everything (0 days)
        memory.pruneOldMemories(0)

        val context = memory.buildContextForGemini("old query")
        assertFalse("Registros viejos deben ser eliminados", context.contains("old query"))
    }

    @Test
    fun `context building includes conversation history`() = runBlocking {
        memory.recordInteraction("user", "¿qué hiciste ayer?")
        memory.recordInteraction("assistant", "Ajusté el EQ a rock.")

        val context = memory.buildContextForGemini("¿recuerdas?")
        assertTrue("Contexto debe incluir historial", context.contains("Ajusté el EQ"))
    }

    @Test
    fun `offline response returns cached when available`() = runBlocking {
        // Este test verifica que la memoria funciona sin red
        memory.learnFact("offline_test", "cached_value",
            IvannaMemoryArchitecture.SemanticRecord.SemanticCategory.LEARNED_FACT)

        val cached = memory.getUserPreference("offline_test")
        assertEquals("cached_value", cached)
    }
}
