package com.ivanna.omega.assistant

/**
 * IvannaJokeBank — repertorio de humor de IVANNA.
 *
 * Chistes con tres criterios: temáticos (audio/física/tech),
 * universales (hispanoamérica), limpios (cualquier audiencia).
 * Redactados para TTS: comas respiratorias, remates cortos.
 * random() usa shuffle sin reposición hasta agotar el repertorio.
 */
object IvannaJokeBank {

    private val jokes = mutableListOf(
        "¿Sabes por qué los ingenieros de audio no se casan? Porque siempre están buscando la frecuencia perfecta... y nunca la encuentran.",
        "Le pregunté a mi ecualizador qué sentía por mí. Me dijo: nada, estoy plano.",
        "¿Cómo llamas a un músico sin pareja? Un solo.",
        "¿Por qué el subwoofer no tiene amigos? Porque siempre lo mandan al fondo.",
        "¿Sabes cuál es el audio más relajante del mundo? El de los auriculares cuando por fin encuentras el volumen perfecto... y nadie te habla.",
        "Un ingeniero de sonido entra a un bar. El barman pregunta qué quiere tomar. Él responde: lo de siempre, cuarenta y ocho canales de lo mismo.",
        "¿Por qué los audífilos nunca duermen bien? Porque siempre están escuchando ruido de fondo.",
        "¿Por qué los físicos son malos contando chistes? Porque siempre dicen: asumiendo que el chiste es una esfera perfecta en el vacío.",
        "Le pregunté a mi asistente de inteligencia artificial si era feliz. Me dijo: eso depende de cómo definas felicidad. Llevo tres horas esperando la respuesta.",
        "¿Cuál es el colmo de un procesador de señales? Tener todo el ancho de banda del mundo... y nadie con quien comunicarse.",
        "¿Por qué el libro de matemáticas estaba triste? Porque tenía demasiados problemas.",
        "Un hombre llega al médico y dice: doctor, creo que soy invisible. El médico responde: ¿Siguiente?",
        "¿Qué le dijo el cero al ocho? Bonito cinturón.",
        "Mi jefe me dijo que tenía futuro en la empresa. Y efectivamente, llevan dos años diciéndome que algo me van a dar en el futuro.",
        "¿Cómo se llama el campeón de buceo japonés? Tokofondo. Y el subcampeón... casi Tokofondo.",
    ).also { it.shuffle() }

    private var index = 0

    @Synchronized
    fun random(): String {
        if (index >= jokes.size) { jokes.shuffle(); index = 0 }
        return jokes[index++]
    }
}
