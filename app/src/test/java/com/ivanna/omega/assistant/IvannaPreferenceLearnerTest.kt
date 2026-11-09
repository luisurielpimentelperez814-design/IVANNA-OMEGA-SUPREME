package com.ivanna.omega.assistant

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

/**
 * Tests del auto-refinamiento por aprendizaje de preferencias.
 *
 * Cubre la política pura de promoción (sin Android) y el learner con
 * persistencia real bajo Robolectric (coalescencia de arrastre, promoción
 * a default aprendido, bloqueo ante reversión).
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33])
class IvannaPreferenceLearnerTest {

    private lateinit var context: Context

    @Before
    fun setup() {
        context = ApplicationProvider.getApplicationContext()
        context.getSharedPreferences("ivanna_preference_learner", Context.MODE_PRIVATE)
            .edit().clear().commit()
        context.getSharedPreferences("ivanna_listener_profile", Context.MODE_PRIVATE)
            .edit().clear().commit()
    }

    private fun adj(value: Float, day: Long) =
        IvannaPreferenceLearner.PromotionPolicy.Adjustment(value, day)

    // ── PromotionPolicy (pura) ───────────────────────────────────────────────

    @Test
    fun `promociona tras 3 ajustes en la misma direccion en 2 dias`() {
        val history = listOf(adj(1.60f, 100L), adj(1.62f, 100L), adj(1.58f, 101L))
        val p = IvannaPreferenceLearner.PromotionPolicy.decide(history, 1.32f, 0.5f, 2.0f, 0.05f)
        assertNotNull("Debe promocionar con evidencia suficiente", p)
        assertEquals(1.60f, p!!.value, 0.01f)
        assertEquals(3, p.evidence)
        assertEquals(2, p.days)
    }

    @Test
    fun `no promociona con menos de 3 ajustes`() {
        val history = listOf(adj(1.60f, 100L), adj(1.62f, 101L))
        assertNull(IvannaPreferenceLearner.PromotionPolicy.decide(history, 1.32f, 0.5f, 2.0f, 0.05f))
    }

    @Test
    fun `no promociona si todo ocurre en un solo dia`() {
        val history = listOf(adj(1.60f, 100L), adj(1.62f, 100L), adj(1.58f, 100L))
        assertNull(IvannaPreferenceLearner.PromotionPolicy.decide(history, 1.32f, 0.5f, 2.0f, 0.05f))
    }

    @Test
    fun `no promociona si las direcciones se mezclan`() {
        val history = listOf(adj(1.60f, 100L), adj(1.00f, 101L), adj(1.58f, 102L))
        assertNull(IvannaPreferenceLearner.PromotionPolicy.decide(history, 1.32f, 0.5f, 2.0f, 0.05f))
    }

    @Test
    fun `la reversion reciente al default bloquea la promocion`() {
        val history = listOf(adj(1.60f, 100L), adj(1.62f, 101L), adj(1.32f, 102L))
        assertNull(IvannaPreferenceLearner.PromotionPolicy.decide(history, 1.32f, 0.5f, 2.0f, 0.05f))
    }

    @Test
    fun `no promociona si los valores no estan agrupados`() {
        val history = listOf(adj(1.45f, 100L), adj(1.90f, 101L), adj(1.50f, 102L))
        assertNull(IvannaPreferenceLearner.PromotionPolicy.decide(history, 1.32f, 0.5f, 2.0f, 0.05f))
    }

    @Test
    fun `la promocion nunca sale del rango del parametro`() {
        val history = listOf(adj(1.99f, 100L), adj(2.00f, 101L), adj(1.98f, 102L))
        val p = IvannaPreferenceLearner.PromotionPolicy.decide(history, 1.32f, 0.5f, 2.0f, 0.05f)
        assertNotNull(p)
        assertTrue("Promoción acotada a [0.5, 2.0]: ${p!!.value}", p.value in 0.5f..2.0f)
    }

    // ── Learner (persistencia + coalescencia) ────────────────────────────────

    @Test
    fun `sin ajustes el default aprendido es el de fabrica`() {
        val learner = IvannaPreferenceLearner(context)
        assertEquals(1.32f, learner.learnedDefault(IvannaPreferenceLearner.SPATIAL_WIDTH), 0.001f)
    }

    @Test
    fun `un gesto de arrastre se fusiona en un solo registro`() {
        val learner = IvannaPreferenceLearner(context)
        learner.recordManualAdjustment(IvannaPreferenceLearner.SPATIAL_WIDTH, 1.50f)
        learner.recordManualAdjustment(IvannaPreferenceLearner.SPATIAL_WIDTH, 1.55f)
        learner.recordManualAdjustment(IvannaPreferenceLearner.SPATIAL_WIDTH, 1.60f)
        assertEquals(
            "Tres onValueChange seguidos deben fusionarse en un registro",
            1, learner.adjustmentCount(IvannaPreferenceLearner.SPATIAL_WIDTH)
        )
    }

    @Test
    fun `el historial persiste entre instancias`() {
        val learner = IvannaPreferenceLearner(context)
        learner.recordManualAdjustment(IvannaPreferenceLearner.EXCITER_AMOUNT, 0.70f)
        val learner2 = IvannaPreferenceLearner(context)
        assertEquals(1, learner2.adjustmentCount(IvannaPreferenceLearner.EXCITER_AMOUNT))
    }

    @Test
    fun `parametro desconocido se ignora sin romper nada`() {
        val learner = IvannaPreferenceLearner(context)
        learner.recordManualAdjustment("parametro_inexistente", 9.99f)
        assertEquals(0f, learner.learnedDefault("parametro_inexistente"), 0.001f)
        assertEquals("Sin defaults aprendidos todavía.", learner.promotionReport())
    }
}
