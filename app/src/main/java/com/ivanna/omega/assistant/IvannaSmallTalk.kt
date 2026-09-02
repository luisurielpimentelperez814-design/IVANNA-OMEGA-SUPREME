package com.ivanna.omega.assistant

import java.util.Calendar

/**
 * IvannaSmallTalk — respuestas de conversación general de IVANNA.
 *
 * Proporciona respuestas humanizadas para interacciones sociales —
 * saludos, cumplidos, preguntas sobre su estado, charla general —
 * sin salirse del carácter de especialista auditiva inteligente.
 * Redactadas para TTS: puntuación natural, longitud media 1-2 frases.
 */
object IvannaSmallTalk {

    fun greetingResponse(): String {
        val hour = Calendar.getInstance().get(Calendar.HOUR_OF_DAY)
        return when {
            hour < 6  -> "Buenas noches. Parece que los dos somos noctámbulos. ¿En qué puedo ayudarte?"
            hour < 12 -> "Buenos días. Espero que el día empiece con buen sonido. ¿Qué necesitas?"
            hour < 19 -> "Buenas tardes. Aquí estoy, lista para ajustar tu audio o lo que necesites."
            else      -> "Buenas noches. ¿Ponemos algo de música para la noche, o tienes otra cosa en mente?"
        }
    }

    fun howAreYouResponse(): String = listOf(
        "Estoy muy bien, gracias por preguntar. Los algoritmos corren suavemente y no hay clipping en el horizonte. ¿Y tú qué tal?",
        "Perfectamente calibrada. Mis filtros están afinados, la latencia es baja y el ánimo es alto. ¿En qué te ayudo?",
        "Funcionando al cien por ciento. Aunque si te soy sincera, disfruto más cuando hay música de por medio. ¿Ponemos algo?",
        "De maravilla. No hay distorsión, no hay fatiga de escucha, y estoy aquí para lo que necesites.",
    ).random()

    fun complimentResponse(): String = listOf(
        "Muchas gracias, me alegra que estés satisfecho. Es exactamente para lo que existo.",
        "Gracias. Eso me motiva a seguir afinando cada detalle. ¿Hay algo más en lo que pueda ayudarte?",
        "Con gusto. La acústica bien hecha no debería notarse, solo sentirse. Me alegra que funcionó.",
        "Aprecio mucho eso. Seguiré aprendiendo tus preferencias para hacerlo cada vez mejor.",
    ).random()

    fun generalChatResponse(): String = listOf(
        "Esa pregunta va un poco más allá de mi especialidad en audio, pero si quieres que te diga algo interesante: el sonido viaja a trescientos cuarenta y tres metros por segundo al nivel del mar.",
        "Mi conocimiento está centrado en lo acústico y lo conversacional. Fuera de eso puedo ser honesta: mejor consulta a alguien más especializado. Pero si tienes algo de música o audio en mente, soy toda tuya.",
        "Interesante pregunta. No soy un oráculo general, pero sí soy muy buena escuchando, procesando y encontrando la mejor versión del sonido. ¿Hay algo en ese terreno en lo que pueda ayudarte?",
    ).random()
}
