package com.ivanna.omega.hardware

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class PermissionPolicyTest {

    @Test
    fun `concedido siempre es GRANTED sin importar el resto`() {
        for (rationale in listOf(false, true)) for (before in listOf(false, true))
            assertEquals(PermissionState.GRANTED, PermissionPolicy.decide(true, rationale, before))
    }

    @Test
    fun `primer arranque sin haber pedido nunca no pide solo - queda en ASK`() {
        assertEquals(PermissionState.ASK, PermissionPolicy.decide(false, false, false))
    }

    @Test
    fun `denegado una vez con explicacion sugerida permite volver a pedir`() {
        assertEquals(PermissionState.SHOW_RATIONALE, PermissionPolicy.decide(false, true, true))
    }

    @Test
    fun `denegado para siempre o revocado en Ajustes manda a Ajustes`() {
        assertEquals(PermissionState.OPEN_SETTINGS, PermissionPolicy.decide(false, false, true))
    }

    @Test
    fun `el mensaje de Ajustes nombra el permiso y aclara que el resto sigue funcionando`() {
        val msg = PermissionPolicy.micDeniedMessage(PermissionState.OPEN_SETTINGS)
        assertTrue(msg.contains("Micrófono"))
        assertTrue(msg.contains("sigue funcionando"))
    }

    @Test
    fun `pickSupportedRate elige la primera frecuencia con minBuffer positivo`() {
        val rate = PermissionPolicy.pickSupportedRate(listOf(48_000, 44_100, 16_000)) {
            if (it == 48_000) -2 else 3_528        // ERROR_BAD_VALUE en 48k
        }
        assertEquals(44_100, rate)
    }

    @Test
    fun `pickSupportedRate trata ERROR y BAD_VALUE como no soportado y devuelve null si ninguna sirve`() {
        assertNull(PermissionPolicy.pickSupportedRate(listOf(48_000, 44_100)) { -1 })
        assertNull(PermissionPolicy.pickSupportedRate(listOf(48_000)) { 0 })
        assertNull(PermissionPolicy.pickSupportedRate(emptyList()) { 100 })
    }
}
