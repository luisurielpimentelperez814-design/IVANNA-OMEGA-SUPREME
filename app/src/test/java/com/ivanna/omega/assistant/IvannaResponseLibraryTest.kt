package com.ivanna.omega.assistant

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

/**
 * Tests de la biblioteca de miles de respuestas (v2.5.371).
 *
 * La biblioteca es solo datos + selección (sin Android, sin red, sin
 * Gemini): los tests verifican el conteo exacto, la determinismo por
 * seed, la cobertura completa de combinaciones y que cada respuesta
 * sea coherente (frases completas, sin placeholders).
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33])
class IvannaResponseLibraryTest {

    @Test
    fun totalResponses_is7640() {
        assertEquals(7640L, IvannaResponseLibrary.totalResponses())
    }

    @Test
    fun responseCount_perCategory() {
        assertEquals(1152L, IvannaResponseLibrary.responseCount(IvannaResponseLibrary.ResponseCategory.DECISION))
        assertEquals(1152L, IvannaResponseLibrary.responseCount(IvannaResponseLibrary.ResponseCategory.ACTION))
        assertEquals(1152L, IvannaResponseLibrary.responseCount(IvannaResponseLibrary.ResponseCategory.APPLICATION))
        assertEquals(1152L, IvannaResponseLibrary.responseCount(IvannaResponseLibrary.ResponseCategory.IMAGINATION))
        assertEquals(3000L, IvannaResponseLibrary.responseCount(IvannaResponseLibrary.ResponseCategory.KNOWLEDGE))
        assertEquals(32L, IvannaResponseLibrary.responseCount(IvannaResponseLibrary.ResponseCategory.SMALLTALK))
    }

    @Test
    fun respond_isDeterministic() {
        for (category in IvannaResponseLibrary.ResponseCategory.values()) {
            val first = IvannaResponseLibrary.respond(category, 12345L)
            val second = IvannaResponseLibrary.respond(category, 12345L)
            assertEquals(first, second)
            assertTrue(first.isNotBlank())
        }
    }

    @Test
    fun respond_coversAllCombinationsWithoutCollision() {
        // Cada seed en [0, count) debe dar una respuesta distinta:
        // es la prueba de que el espacio de combinaciones es real.
        for (category in IvannaResponseLibrary.ResponseCategory.values()) {
            val count = IvannaResponseLibrary.responseCount(category)
            val seen = HashSet<String>()
            var seed = 0L
            while (seed < count) {
                seen.add(IvannaResponseLibrary.respond(category, seed))
                seed++
            }
            assertEquals(count.toInt(), seen.size)
        }
    }

    @Test
    fun randomResponse_everyCategoryIsNonBlank() {
        for (category in IvannaResponseLibrary.ResponseCategory.values()) {
            repeat(20) {
                val response = IvannaResponseLibrary.randomResponse(category)
                assertTrue(response.isNotBlank())
                assertTrue(response.endsWith(".") || response.endsWith("?"))
                assertTrue(!response.contains("{") && !response.contains("\$"))
            }
        }
    }

    @Test
    fun respondByName_mapsSpanishNames() {
        assertTrue(IvannaResponseLibrary.respondByName("decisión").isNotBlank())
        assertTrue(IvannaResponseLibrary.respondByName("accion").isNotBlank())
        assertTrue(IvannaResponseLibrary.respondByName("aplicacion").isNotBlank())
        assertTrue(IvannaResponseLibrary.respondByName("imaginacion").isNotBlank())
        assertTrue(IvannaResponseLibrary.respondByName("conocimiento").isNotBlank())
        assertTrue(IvannaResponseLibrary.respondByName("charla").isNotBlank())
        // Nombre desconocido: no falla, devuelve una respuesta cualquiera.
        assertTrue(IvannaResponseLibrary.respondByName("noexiste").isNotBlank())
    }

    @Test
    fun countSummary_mentionsTotal() {
        assertTrue(IvannaResponseLibrary.countSummary().contains("TOTAL=7640"))
    }
}
