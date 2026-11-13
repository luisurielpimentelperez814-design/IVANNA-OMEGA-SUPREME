package com.ivanna.omega.saf

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Mensaje de entrega del perfil SAF (v2.5.429).
 *
 * Contrato: el mensaje reporta la aplicación LOCAL en Ruta A, nunca el
 * daemon (muerto, sin Magisk). El acoplamiento SAF de sala se aplica en
 * cualquier ruta; el sujeto HRTF (binaural) solo está activo en auriculares.
 * La puerta de ruta NO debe bloquear el push local (eso dejaba la
 * calibración sin efecto audible en bocina/USB).
 */
class SaFDeliveryMessageTest {

    @Test fun safDisabledReportsSavedButNotApplied() {
        val msg = safDeliveryMessage(safEnabled = false, hrtfActive = false, subject = "kemar")
        assertTrue(msg.contains("desactivado", ignoreCase = true))
        assertTrue(msg.contains("guardado", ignoreCase = true))
        assertFalse(msg.contains("aplicado", ignoreCase = true))
    }

    @Test fun headphonesReportsPersonalHrtfActiveWithSubject() {
        val msg = safDeliveryMessage(safEnabled = true, hrtfActive = true, subject = "cipic_003")
        assertTrue(msg.contains("Ruta A"))
        assertTrue(msg.contains("HRTF personalizado"))
        assertTrue(msg.contains("cipic_003"))
        assertTrue(msg.contains("auriculares", ignoreCase = true))
    }

    @Test fun speakersReportsRoomCouplingAndHrtfHeadphonesOnly() {
        val msg = safDeliveryMessage(safEnabled = true, hrtfActive = false, subject = "kemar")
        assertTrue(msg.contains("Ruta A"))
        assertTrue(msg.contains("acoplamiento de sala", ignoreCase = true))
        assertTrue(msg.contains("kemar"))
        assertTrue(msg.contains("solo en auriculares", ignoreCase = true))
    }

    @Test fun blankSubjectFallsBackToKemar() {
        val msg = safDeliveryMessage(safEnabled = true, hrtfActive = true, subject = "   ")
        assertTrue(msg.contains("kemar"))
    }

    @Test fun noneSubjectFallsBackToKemar() {
        // "none" = renderer sin HRTF cargado todavía; se muestra el default.
        val msg = safDeliveryMessage(safEnabled = true, hrtfActive = false, subject = "none")
        assertTrue(msg.contains("kemar"))
        assertFalse(msg.contains("(none)"))
    }

    @Test fun messageNeverMentionsTheDaemon() {
        // El daemon está muerto; el mensaje no debe prometer ni culpar nada
        // que dependa de él (ni "control global" ni "daemon").
        for (msg in listOf(
            safDeliveryMessage(false, false, "kemar"),
            safDeliveryMessage(true, true, "kemar"),
            safDeliveryMessage(true, false, "kemar")
        )) {
            assertFalse(msg.contains("daemon", ignoreCase = true))
            assertFalse(msg.contains("control global", ignoreCase = true))
        }
    }
}
