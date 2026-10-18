package com.ivanna.omega.assistant

import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33])
class IvannaLanguageCoreBroadChatTest {

    @Test
    fun unfamiliarGeneralTopicRoutesToConversationalAgent() {
        val parsed = IvannaLanguageCore.parse("Háblame de la fotosíntesis y cómo funciona")

        assertEquals(IvannaLanguageCore.AcousticIntent.GENERAL_CHAT, parsed.acousticIntent)
    }

    @Test
    fun explicitAudioCommandStillUsesAudioIntent() {
        val parsed = IvannaLanguageCore.parse("sube el volumen un poco")

        assertEquals(IvannaLanguageCore.AcousticIntent.VOLUME_UP, parsed.acousticIntent)
    }
}
