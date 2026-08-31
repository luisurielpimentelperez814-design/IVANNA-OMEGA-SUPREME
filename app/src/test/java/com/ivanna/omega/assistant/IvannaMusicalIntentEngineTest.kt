package com.ivanna.omega.assistant

import org.junit.Assert.*
import org.junit.Test

/**
 * Pruebas unitarias para IvannaMusicalIntentEngine.
 *
 * Verifica que las expresiones musicales humanas se traducen correctamente
 * a presets DSP. Sin dependencias de Android (solo lógica pura).
 */
class IvannaMusicalIntentEngineTest {

    @Test
    fun `épico detecta preset ÉPICO`() {
        val result = IvannaMusicalIntentEngine.detect("hazlo épico")
        assertNotNull("'épico' debe detectar preset", result)
        assertEquals("ÉPICO", result!!.name)
    }

    @Test
    fun `magistral detecta preset ÉPICO`() {
        val result = IvannaMusicalIntentEngine.detect("configúralo magistralmente")
        assertNotNull("'magistral' debe detectar preset ÉPICO", result)
        assertEquals("ÉPICO", result!!.name)
    }

    @Test
    fun `abbey road detecta preset ABBEY ROAD`() {
        val result = IvannaMusicalIntentEngine.detect("como Abbey Road")
        assertNotNull("'Abbey Road' debe detectar preset", result)
        assertEquals("ABBEY ROAD", result!!.name)
    }

    @Test
    fun `vinilo detecta preset VINILO PREMIUM`() {
        val result = IvannaMusicalIntentEngine.detect("como vinilo premium")
        assertNotNull("'vinilo' debe detectar preset", result)
        assertEquals("VINILO PREMIUM", result!!.name)
    }

    @Test
    fun `concierto masivo detecta preset CONCIERTO MASIVO`() {
        val result = IvannaMusicalIntentEngine.detect("como un concierto gigantesco")
        assertNotNull("'concierto gigantesco' debe detectar preset", result)
        assertEquals("CONCIERTO MASIVO", result!!.name)
    }

    @Test
    fun `cinematográfico detecta preset CINEMATOGRÁFICO`() {
        val result = IvannaMusicalIntentEngine.detect("más cinematográfico")
        assertNotNull("'cinematográfico' debe detectar preset", result)
        assertEquals("CINEMATOGRÁFICO", result!!.name)
    }

    @Test
    fun `analógico detecta preset ANALÓGICO`() {
        val result = IvannaMusicalIntentEngine.detect("más analógico")
        assertNotNull("'analógico' debe detectar preset", result)
        assertEquals("ANALÓGICO", result!!.name)
    }

    @Test
    fun `estudio profesional detecta preset ESTUDIO PRO`() {
        val result = IvannaMusicalIntentEngine.detect("crea una configuración de estudio profesional")
        assertNotNull("'estudio profesional' debe detectar preset", result)
        assertEquals("ESTUDIO PRO", result!!.name)
    }

    @Test
    fun `quiero escuchar detalles detecta preset MICRODETALLE`() {
        val result = IvannaMusicalIntentEngine.detect("quiero escuchar detalles que normalmente no escucho")
        assertNotNull("'quiero escuchar detalles' debe detectar preset", result)
        assertEquals("MICRODETALLE", result!!.name)
    }

    @Test
    fun `más cálido detecta preset CÁLIDO`() {
        val result = IvannaMusicalIntentEngine.detect("hazlo más cálido")
        assertNotNull("'más cálido' debe detectar preset", result)
        assertEquals("CÁLIDO", result!!.name)
    }

    @Test
    fun `más espacial detecta preset ESPACIAL`() {
        val result = IvannaMusicalIntentEngine.detect("hazlo más espacial")
        assertNotNull("'más espacial' debe detectar preset", result)
        assertEquals("ESPACIAL", result!!.name)
    }

    @Test
    fun `texto irrelevante no detecta preset`() {
        val result = IvannaMusicalIntentEngine.detect("pon el volumen")
        assertNull("Texto no musical no debe detectar preset", result)
    }

    @Test
    fun `texto vacío no detecta preset`() {
        val result = IvannaMusicalIntentEngine.detect("")
        assertNull("Texto vacío no debe detectar preset", result)
    }

    @Test
    fun `todos los presets tienen explicación no vacía`() {
        val testInputs = listOf(
            "épico", "abbey road", "vinilo premium", "concierto gigantesco",
            "cinematográfico", "analógico", "estudio profesional",
            "quiero escuchar detalles", "más cálido", "más espacial"
        )
        testInputs.forEach { input ->
            val preset = IvannaMusicalIntentEngine.detect(input)
            if (preset != null) {
                assertTrue(
                    "Preset para '$input' debe tener explanation no vacía",
                    preset.explanation.isNotBlank()
                )
                assertTrue(
                    "Preset para '$input' debe tener technicalDetail no vacío",
                    preset.technicalDetail.isNotBlank()
                )
            }
        }
    }

    @Test
    fun `availablePresetsDescription no está vacío`() {
        val desc = IvannaMusicalIntentEngine.availablePresetsDescription()
        assertTrue("availablePresetsDescription debe tener contenido", desc.isNotBlank())
        assertTrue("Debe mencionar al menos 5 presets", desc.split(",").size >= 5)
    }

    // ── Tests adicionales de la fase de cierre ───────────────────────────────

    @Test
    fun `concierto en vivo detecta preset CONCIERTO MASIVO`() {
        val result = IvannaMusicalIntentEngine.detect("como concierto en vivo")
        assertNotNull("'concierto en vivo' debe detectar preset", result)
        assertEquals("CONCIERTO MASIVO", result!!.name)
    }

    @Test
    fun `brutal detecta preset ÉPICO`() {
        val result = IvannaMusicalIntentEngine.detect("suena brutal")
        assertNotNull("'brutal' debe detectar preset ÉPICO", result)
        assertEquals("ÉPICO", result!!.name)
    }

    @Test
    fun `frankenstein frase compleja detecta preset valido`() {
        // "Frankenstein de Edgar Winter suena brutal pero quiero más escenario"
        // → "brutal" activa ÉPICO antes que "escenario" active CONCIERTO MASIVO
        // (orden de detección del engine). Ambos son válidos como respuesta.
        val result = IvannaMusicalIntentEngine.detect(
            "Frankenstein de Edgar Winter suena brutal pero quiero más escenario"
        )
        assertNotNull("Frase compleja de Frankenstein debe detectar algún preset", result)
        val validPresets = setOf("ÉPICO", "CONCIERTO MASIVO")
        assertTrue(
            "Frankenstein debe producir preset épico o de estadio, fue: ${result!!.name}",
            result.name in validPresets
        )
    }

    @Test
    fun `frankenstein epico tiene spatialBoost no negativo`() {
        val result = IvannaMusicalIntentEngine.detect("hazlo épico")
        assertNotNull(result)
        assertTrue(
            "El preset épico debe tener spatialBoost >= 0",
            result!!.spatialBoost >= 0f
        )
    }

    @Test
    fun `primera fila detecta preset PRIMERA FILA`() {
        val result = IvannaMusicalIntentEngine.detect("como si estuviera ahí en primera fila")
        assertNotNull("'primera fila' debe detectar preset", result)
        assertEquals("PRIMERA FILA", result!!.name)
    }

    @Test
    fun `quiero escuchar mas detalles detecta MICRODETALLE`() {
        val result = IvannaMusicalIntentEngine.detect("quiero escuchar más detalles")
        assertNotNull("'quiero escuchar más detalles' debe detectar preset", result)
        assertEquals("MICRODETALLE", result!!.name)
    }

    @Test
    fun `preset epico tiene extraCommand concert_mode`() {
        val result = IvannaMusicalIntentEngine.detect("hazlo épico")
        assertNotNull(result)
        assertEquals("concert_mode", result!!.extraCommand)
    }

    @Test
    fun `preset concierto masivo tiene spatialBoost alto`() {
        val result = IvannaMusicalIntentEngine.detect("como un concierto gigantesco")
        assertNotNull(result)
        assertTrue("Concierto masivo debe tener spatialBoost >= 0.4f", result!!.spatialBoost >= 0.4f)
    }

    @Test
    fun `preset espacial tiene spatialBoost positivo`() {
        val result = IvannaMusicalIntentEngine.detect("hazlo más espacial")
        assertNotNull(result)
        assertTrue("Espacial debe tener spatialBoost > 0", result!!.spatialBoost > 0f)
    }

    @Test
    fun `preset abbey road no tiene extraCommand`() {
        val result = IvannaMusicalIntentEngine.detect("como Abbey Road")
        assertNotNull(result)
        assertNull("Abbey Road no debe tener comando extra", result!!.extraCommand)
    }
}
