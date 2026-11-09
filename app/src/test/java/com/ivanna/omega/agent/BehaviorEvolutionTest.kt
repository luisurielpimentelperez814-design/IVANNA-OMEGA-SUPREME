package com.ivanna.omega.agent

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

/**
 * Tests de la auto-evolución acotada (BehaviorEvolution) y de la matriz de
 * autoridad de decisión (IvannaDecisionAuthority).
 *
 * Cubre: defaults fail-closed, pasos acotados al rango, retorno hacia el
 * default tras éxito, y los veredictos de autorización (autónomo / necesita
 * usuario / denegado / fuera de rango / veto).
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33])
class BehaviorEvolutionTest {

    private lateinit var context: Context

    @Before
    fun setup() {
        context = ApplicationProvider.getApplicationContext()
        context.getSharedPreferences("ivanna_behavior_evolution", Context.MODE_PRIVATE)
            .edit().clear().commit()
        context.getSharedPreferences("ivanna_decision_authority", Context.MODE_PRIVATE)
            .edit().clear().commit()
        BehaviorEvolution.init(context)
        IvannaDecisionAuthority.init(context)
    }

    // ── BehaviorEvolution ────────────────────────────────────────────────────

    @Test
    fun `sin inicializar ni adaptar el piso es el default`() {
        assertEquals(0.65f, BehaviorEvolution.get(BehaviorEvolution.CLIP_GAIN_FLOOR), 0.001f)
    }

    @Test
    fun `el fracaso repetido baja el piso un paso cada vez hasta el minimo`() {
        repeat(2) { BehaviorEvolution.adaptOnRepeatedFailure(BehaviorEvolution.CLIP_GAIN_FLOOR) }
        assertEquals(0.55f, BehaviorEvolution.get(BehaviorEvolution.CLIP_GAIN_FLOOR), 0.001f)
        // Tercer intento: ya en el mínimo, no baja más.
        BehaviorEvolution.adaptOnRepeatedFailure(BehaviorEvolution.CLIP_GAIN_FLOOR)
        assertEquals(0.55f, BehaviorEvolution.get(BehaviorEvolution.CLIP_GAIN_FLOOR), 0.001f)
    }

    @Test
    fun `el exito repetido devuelve el piso hacia el default sin pasarlo`() {
        BehaviorEvolution.adaptOnRepeatedFailure(BehaviorEvolution.CLIP_GAIN_FLOOR)
        assertEquals(0.60f, BehaviorEvolution.get(BehaviorEvolution.CLIP_GAIN_FLOOR), 0.001f)
        BehaviorEvolution.adaptOnSuccess(BehaviorEvolution.CLIP_GAIN_FLOOR)
        assertEquals(0.65f, BehaviorEvolution.get(BehaviorEvolution.CLIP_GAIN_FLOOR), 0.001f)
        // En el default, el éxito no lo mueve hacia el lado arriesgado.
        BehaviorEvolution.adaptOnSuccess(BehaviorEvolution.CLIP_GAIN_FLOOR)
        assertEquals(0.65f, BehaviorEvolution.get(BehaviorEvolution.CLIP_GAIN_FLOOR), 0.001f)
    }

    @Test
    fun `parametro desconocido devuelve cero sin romper nada`() {
        assertEquals(0f, BehaviorEvolution.get("parametro_inexistente"), 0.001f)
    }

    // ── IvannaDecisionAuthority ──────────────────────────────────────────────

    @Test
    fun `una reparacion autonoma dentro de rango esta permitida`() {
        val auth = IvannaDecisionAuthority.authorize(
            "repair_clip_gain_cut", mapOf("targetGain" to 0.65f)
        )
        assertEquals(IvannaDecisionAuthority.Verdict.ALLOWED, auth.verdict)
    }

    @Test
    fun `una politica con parametros fuera de rango se deniega aunque sea autonoma`() {
        val auth = IvannaDecisionAuthority.authorize(
            "policy_apply", mapOf("targetGain" to 1.5f, "spatialWidth" to 1.0f)
        )
        assertEquals(IvannaDecisionAuthority.Verdict.DENIED, auth.verdict)
    }

    @Test
    fun `borrar memoria requiere confirmacion del usuario`() {
        val auth = IvannaDecisionAuthority.authorize("clear_memory")
        assertEquals(IvannaDecisionAuthority.Verdict.NEEDS_USER, auth.verdict)
    }

    @Test
    fun `desactivar el gate de integridad esta prohibido siempre`() {
        val auth = IvannaDecisionAuthority.authorize("disable_integrity_gate")
        assertEquals(IvannaDecisionAuthority.Verdict.DENIED, auth.verdict)
    }

    @Test
    fun `una accion sin tier declarado requiere usuario por defecto`() {
        val auth = IvannaDecisionAuthority.authorize("accion_desconocida_xyz")
        assertEquals(IvannaDecisionAuthority.Verdict.NEEDS_USER, auth.verdict)
    }

    @Test
    fun `el veto del usuario bloquea re-aplicar la misma decision`() {
        val params = mapOf("spatialWidth" to 1.50f)
        IvannaDecisionAuthority.recordDecision("preference_promotion", params, true, "promoción test")
        IvannaDecisionAuthority.markUndone("preference_promotion", params)
        val auth = IvannaDecisionAuthority.authorize("preference_promotion", params)
        assertEquals(IvannaDecisionAuthority.Verdict.NEEDS_USER, auth.verdict)
        assertTrue(IvannaDecisionAuthority.isVetoed("preference_promotion", params))
    }

    @Test
    fun `el veto no bloquea el corte anti-clip de seguridad`() {
        val params = mapOf("targetGain" to 0.60f)
        IvannaDecisionAuthority.markUndone("repair_clip_gain_cut", params)
        val auth = IvannaDecisionAuthority.authorize("repair_clip_gain_cut", params)
        assertEquals(IvannaDecisionAuthority.Verdict.ALLOWED, auth.verdict)
    }

    @Test
    fun `las decisiones quedan registradas y el reporte las resume`() {
        IvannaDecisionAuthority.recordDecision("optimize", emptyMap(), true, "optimización test")
        val recent = IvannaDecisionAuthority.recentDecisions(4)
        assertEquals(1, recent.size)
        assertEquals("optimize", recent.first().action)
        assertTrue(IvannaDecisionAuthority.report().contains("optimize"))
    }
}
