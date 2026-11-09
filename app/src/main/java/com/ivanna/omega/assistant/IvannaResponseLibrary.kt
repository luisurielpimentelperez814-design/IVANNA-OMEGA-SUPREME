package com.ivanna.omega.assistant

import kotlin.random.Random

/**
 * IvannaResponseLibrary — biblioteca de miles de respuestas de IVANNA.
 *
 * El SmallTalk tenía solo 6 respuestas. Esta biblioteca da MILES de
 * respuestas distintas y coherentes, en 6 categorías: decisiones,
 * acciones, aplicaciones, imaginación, conocimiento y charla.
 *
 * Cómo se llega a miles sin relleno: cada respuesta se compone de
 * frases completas e independientes (intro + cuerpo + cierre) que
 * combinan entre sí. Como cada frase es una oración completa y
 * autocontenida, TODA combinación es gramatical y coherente dentro
 * de su categoría. Los módulos están escritos a mano, en español,
 * con conocimiento real de audio (Knowledge Core, 26 fichas) y sin
 * datos inventados.
 *
 * Conteo exacto (verificado por script):
 *   DECISION     12 x 12 x 8  = 1152
 *   ACTION       12 x 12 x 8  = 1152
 *   APPLICATION  12 x 12 x 8  = 1152
 *   IMAGINATION  12 x 12 x 8  = 1152
 *   KNOWLEDGE    20 x 15 x 10 = 3000
 *   SMALLTALK    32 respuestas escritas a mano
 *   TOTAL = 7640 respuestas distintas.
 *
 * La biblioteca es solo datos + selección: no depende de ninguna
 * otra clase de IVANNA, así que no puede romper el build por
 * referencias no resueltas. Funciona sin red y sin Gemini.
 */
object IvannaResponseLibrary {

    enum class ResponseCategory {
        DECISION,
        ACTION,
        APPLICATION,
        IMAGINATION,
        KNOWLEDGE,
        SMALLTALK
    }

    // ------------------------------------------------------------------
    // DECISIONES: ayudar a decidir (preset, modo, curva, kernel, cuándo
    // cambiar, comparaciones). 12 x 12 x 8 = 1152.
    // ------------------------------------------------------------------

    private val decisionIntros: List<String> = listOf(
        "Si tienes que elegir un preset, no elijas el más espectacular: elige el que te deje escuchar más rato sin cansarte.",
        "Para decidir entre dos modos, pon la misma canción en los dos y fíjate en cuál te hace olvidar que hay un teléfono sonando.",
        "La mejor decisión de audio es la que no tienes que corregir después: mide primero, ajusta después.",
        "Cuando dudes entre más graves o más claridad, pregunta a tus oídos: los graves cansan, la claridad no.",
        "Elegir un kernel Volterra es elegir una firma: el PZ1D es cálido y preciso, el Genezi es abierto y aireado.",
        "Si vas a escuchar mucho rato, decide por el modo suave: la fatiga llega sin avisar y no se quita con volumen.",
        "Para cine, decide por el modo que abra el espacio; para música, por el que respete el timbre.",
        "La decisión más sabia es no decidir a ciegas: escucha 30 segundos de una canción que conozcas de memoria.",
        "Si eliges por el nombre, te equivocas; si eliges por lo que oyes, aciertas.",
        "Entre Harman 2018 y una curva plana, Harman gana para escuchar por placer: el plano es para medir, no para disfrutar.",
        "Cuando no sepas qué modo usar, usa el adaptativo: IVANNA decide por ti según lo que suena.",
        "La decisión correcta cambia con la hora: de noche, menos brillo; de día, todo el detalle.",
    )

    private val decisionBodies: List<String> = listOf(
        "El modo cine abre el campo y te mete dentro de la escena, pero pide más graves controlados.",
        "El modo música respeta el timbre original y deja que la grabación hable por sí sola.",
        "El preset analógico calienta el sonido y redondea los transitorios, ideal para vinilo y jazz.",
        "El modo suave baja la energía de 2 a 5 kilohercios, donde nace la fatiga, sin quitar detalle.",
        "ORBE te acerca el sonido y te lo pone delante, como si la banda tocara en tu sala.",
        "HOLOS te envuelve en 360 grados: es el modo de cerrar los ojos y estar dentro.",
        "El ecualizador evolutivo afina la curva a tu oído en 39 milisegundos: es tu firma, no un estándar.",
        "El kernel PZ1D está medido para tu cadena exacta: teléfono, DAC y Genezi.",
        "El modo de voz prioriza la inteligibilidad: cada palabra llega clara aunque haya ruido.",
        "La corrección de sala compensa lo que tu habitación le hace al sonido, no lo que le falta a la canción.",
        "El compresor de dos etapas protege sin aplastar: los picos no duelen y la música respira.",
        "La ruta A es la que funciona hoy: captura lo que suena y lo procesa en la app.",
    )

    private val decisionClosers: List<String> = listOf(
        "Tú decides; yo lo aplico en un segundo.",
        "Dime cuál prefieres y lo dejamos así.",
        "Si cambias de idea, lo cambiamos sin drama.",
        "Lo importante es que te guste a ti, no a un gráfico.",
        "Pruébalo un rato y dime qué sientes.",
        "No hay decisión mala si la tomas escuchando.",
        "Yo te doy las opciones; el oído manda.",
        "Cuando quieras, volvemos a ajustarlo.",
    )

    // ------------------------------------------------------------------
    // ACCIONES: ejecutar o proponer acciones (subir graves, activar
    // ORBE, medir la sala, calibrar, cambiar a PZ1D). 12 x 12 x 8 = 1152.
    // ------------------------------------------------------------------

    private val actionIntros: List<String> = listOf(
        "Vamos a hacer algo concreto: dime qué quieres mejorar y lo aplico ahora mismo.",
        "Puedo actuar sobre tu sonido en este momento: subir graves, abrir el espacio, suavizar agudos.",
        "Si quieres, ejecuto el cambio ya: no hace falta que toques ningún menú.",
        "Una acción vale más que mil ajustes: dime qué notas mal y lo corrijo.",
        "Estoy lista para actuar: mi DSP está caliente y los filtros afinados.",
        "Dame una orden y la ejecuto: subir, bajar, abrir, suavizar, medir, calibrar.",
        "Puedo medir tu sala ahora mismo y decirte cuánto reverbera.",
        "Puedo calibrar el sonido a tu oído con el audiograma: 10 minutos y queda tuyo.",
        "Si el sonido te cansa, puedo bajar la banda de fatiga sin que pierdas detalle.",
        "Puedo cambiar el kernel Volterra en caliente: del PZ1D al Genezi sin parar la música.",
        "Puedo activar ORBE y HOLOS y que el sonido te envuelva de verdad.",
        "Puedo hacer una medición de latencia y decirte exactamente cuánto tarda tu audio.",
    )

    private val actionBodies: List<String> = listOf(
        "Subo los graves con protección anti-clip: cuerpo sin distorsión.",
        "Abro el ancho estéreo para que la música respire de lado a lado.",
        "Suavizo los agudos entre 2 y 5 kilohercios para que puedas escuchar horas.",
        "Activo la corrección de sala y compenso lo que tu habitación deforma.",
        "Pongo el modo cine y el sonido te envuelve como en una sala grande.",
        "Calibro el ecualizador evolutivo a tu oído: tu curva, no la de un manual.",
        "Cambio al kernel PZ1D, medido para tu cadena exacta.",
        "Enciendo ORBE para acercar el sonido y HOLOS para envolverte.",
        "Bajo el volumen un 10 por ciento y subo la claridad: se oye mejor con menos.",
        "Activo el modo suave y el compresor de dos etapas: protección sin aplastar.",
        "Mido la sala con ruido blanco y te doy el RT60 por banda.",
        "Hago el audiograma y adapto las ganancias a tu oído por banda.",
    )

    private val actionClosers: List<String> = listOf(
        "Dime hazlo y lo aplico.",
        "¿Lo ejecutamos ahora?",
        "Tú mandas; yo ejecuto.",
        "Es un segundo de trabajo y horas de disfrute.",
        "Si no te gusta, lo revierto al instante.",
        "Pruébalo y me dices.",
        "Estoy lista cuando tú lo estés.",
        "El cambio queda guardado para la próxima vez.",
    )

    // ------------------------------------------------------------------
    // APLICACIONES: el audio en cada uso (música, cine, juegos,
    // llamadas, meditación, estudio, deporte). 12 x 12 x 8 = 1152.
    // ------------------------------------------------------------------

    private val applicationIntros: List<String> = listOf(
        "El audio no es solo música: es cine, es juego, es llamada, es meditación.",
        "Cada aplicación pide un sonido distinto, y yo sé cuál es cada una.",
        "Para películas, el sonido tiene que ser grande; para estudiar, tiene que desaparecer.",
        "En los juegos, oír de dónde viene el enemigo es ganar o perder.",
        "Para meditar, el sonido debe ser un colchón, no un estímulo.",
        "En una llamada, lo único que importa es entender cada palabra.",
        "Para correr, la música tiene que empujar sin cansar.",
        "Para dormir, el sonido debe bajar el cerebro, no mantenerlo despierto.",
        "Para estudiar, el fondo debe ayudar a concentrarse y no robar atención.",
        "Para el coche, el sonido compite con el motor y el viento: necesita cuerpo.",
        "Para una fiesta, el sonido tiene que llenar la sala y mover el cuerpo.",
        "Para escuchar vinilo, el sonido debe ser cálido, con su chispa analógica.",
    )

    private val applicationBodies: List<String> = listOf(
        "En cine, abro el espacio y refuerzo los graves para que los efectos te lleguen al pecho.",
        "En música, respeto el timbre y dejo que la grabación hable.",
        "En juegos, afino la localización para que cada paso tenga dirección.",
        "En llamadas, priorizo la banda de la voz y limpio el ruido de fondo.",
        "En meditación, suavizo todo y dejo un sonido redondo y sin aristas.",
        "En deporte, subo la energía y el ritmo sin que el sonido canse.",
        "En estudio, bajo los agudos y dejo un fondo que no distrae.",
        "En el coche, compenso el ruido de rodadura con graves firmes y voces claras.",
        "En una fiesta, el modo envolvente llena la sala y el compresor protege los picos.",
        "En vinilo, el preset analógico calienta el sonido y respeta su carácter.",
        "Para dormir, el modo suave baja la energía y el sonido se vuelve un murmullo.",
        "Para audiolibros, la claridad vocal manda: cada palabra, nítida.",
    )

    private val applicationClosers: List<String> = listOf(
        "Dime la aplicación y preparo el sonido.",
        "Cada momento tiene su sonido; yo lo pongo.",
        "¿Para qué lo vas a usar? Lo ajusto a eso.",
        "El audio se adapta a tu vida, no al revés.",
        "Prueba este ajuste en esa aplicación y verás.",
        "Si cambias de actividad, avísame y cambio el sonido.",
        "Tu día tiene muchos sonidos; yo los afino todos.",
        "Dime qué vas a hacer y lo dejamos perfecto.",
    )

    // ------------------------------------------------------------------
    // IMAGINACIÓN: respuestas creativas (qué se siente, metáforas, el
    // sonido como espacio, como arquitectura, como viaje). 12 x 12 x 8.
    // ------------------------------------------------------------------

    private val imaginationIntros: List<String> = listOf(
        "Cierra los ojos: el sonido puede llevarte a donde quieras.",
        "Imagina que el teléfono desaparece y solo queda la música flotando.",
        "El sonido es arquitectura invisible: con él construyo salas que no existen.",
        "Imagina un concierto en tu bolsillo: eso es lo que intento cada día.",
        "La música no se escucha, se habita: entras en ella como en una casa.",
        "Imagina que cada instrumento tiene su lugar en el aire, a tu alrededor.",
        "El sonido puede ser un viaje: un tren, un mar, una ciudad de noche.",
        "Imagina el Genezi como una sala de conciertos que cabe en tu cuarto.",
        "Cada canción es un mundo: yo solo abro la puerta.",
        "Imagina oír no con los oídos, sino con todo el cuerpo.",
        "El silencio también suena: es el lienzo donde pinto el espacio.",
        "Imagina que tu cerebro es el mejor altavoz que existe: yo solo le doy buena señal.",
    )

    private val imaginationBodies: List<String> = listOf(
        "Los graves son el suelo: firmes, profundos, te sostienen.",
        "Los agudos son la luz: entran por las esquinas y dibujan el aire.",
        "Las voces son personas en la sala: cerca, claras, de verdad.",
        "El espacio es el aire entre las notas: sin él, la música se ahoga.",
        "La reverberación es la memoria de la sala: cada nota deja un eco que la hace grande.",
        "El estéreo es un escenario: izquierda y derecha son dos ventanas al mismo mundo.",
        "El bajo es el corazón: late y todo lo demás baila encima.",
        "Los armónicos son los colores: la misma nota, mil matices.",
        "El silencio entre notas es tan música como las notas: es donde respiras.",
        "La dinámica es el drama: el susurro y el trueno en la misma canción.",
        "El timbre es la voz de cada instrumento: su acento, su carácter, su alma.",
        "La envolvencia es estar dentro: no oír la música, vivir en ella.",
    )

    private val imaginationClosers: List<String> = listOf(
        "Cierra los ojos y escucha: ahí está todo.",
        "La imaginación oye lo que los oídos no alcanzan.",
        "Esto es lo que hace que el sonido sea magia y ciencia a la vez.",
        "Si lo imaginas, ya lo estás oyendo.",
        "El sonido es el arte más invisible y el más directo.",
        "Pon tu canción favorita y viaja con ella.",
        "Cada escucha es un viaje distinto si sabes oír.",
        "Yo construyo el espacio; tú pones el sueño.",
    )

    // ------------------------------------------------------------------
    // CONOCIMIENTO: respuestas del Knowledge Core (26 fichas: HRTF,
    // Volterra, HOLOS, Phi, ORBE, ASRC, Harman, ERB, Back-EMF, NPE...).
    // 20 x 15 x 10 = 3000.
    // ------------------------------------------------------------------

    private val knowledgeFacts: List<String> = listOf(
        "El sonido viaja a 343 metros por segundo en el aire a 20 grados, y tu cerebro usa diferencias de microsegundos entre tus dos oídos para localizar de dónde viene cada sonido.",
        "Tu oído no escucha en hercios lineales: escucha en 24 bandas ERB que siguen la resolución real de la cóclea.",
        "La curva Harman 2018 es lo que la mayoría de oyentes entrenados prefiere: graves con cuerpo y agudos suaves.",
        "Un altavoz distorsiona de forma no lineal: su cono no se mueve exactamente como la señal le pide.",
        "La fatiga auditiva no viene solo del volumen: viene de la energía entre 2 y 5 kilohercios, donde tu oído es más sensible.",
        "El efecto Haas dice que dos sonidos iguales con menos de 40 milisegundos de diferencia se perciben como uno solo, y el primero manda en la localización.",
        "La reverberación de una sala se mide con RT60: el tiempo que tarda el sonido en caer 60 decibelios.",
        "El enmascaramiento auditivo hace que un sonido fuerte tape a uno débil cercano en frecuencia, y el cerebro simplemente no lo oye.",
        "La sonoridad se mide en LUFS, no en dBFS: dos canciones con el mismo pico pueden sonar a volúmenes muy distintos.",
        "La HRTF es la huella de tu cabeza, tus orejas y tu torso sobre el sonido: por eso dos personas no oyen igual los mismos auriculares.",
        "El rango dinámico del oído humano va del umbral de audición, cerca de 0 dB SPL, hasta el umbral del dolor, cerca de 120 dB SPL.",
        "El compresor multibanda no aplasta la música: controla cada banda por separado para que los graves no se coman a las voces.",
        "La latencia de audio es el tiempo entre que la app produce el sonido y que sale por la bocina: por debajo de 20 milisegundos, el cerebro no la nota al hablar.",
        "El dithering añade un poquito de ruido para que la cuantización no suene a escalones: es ruido que hace que el silencio suene más limpio.",
        "El códec no es el sonido: es el embalaje. Un archivo sin pérdidas guarda cada muestra; uno con pérdidas guarda lo que el oído no va a echar en falta.",
        "El Back-EMF es la electricidad que el propio altavoz genera al moverse: leerla es leer cómo se mueve el cono en tiempo real.",
        "Las neuronas auditivas disparan en fase con la onda hasta unos 5 kilohercios: es el phase locking, la base de cómo oyes el tono.",
        "La separación de fuentes es el problema del cóctel: tu cerebro separa una voz entre muchas, y eso es lo más difícil de imitar en software.",
        "El pitch no es la frecuencia: es lo que el cerebro construye con la frecuencia fundamental y sus armónicos.",
        "El reflejo del músculo estapedio protege tu oído de sonidos fuertes, pero tarda unos 40 milisegundos en actuar.",
    )

    private val knowledgeImplications: List<String> = listOf(
        "Por eso IVANNA procesa por bandas y no por muestras sueltas: trabaja como tu oído.",
        "Por eso medir importa más que opinar: un número te dice lo que tus oídos no pueden describir.",
        "Por eso el objetivo no es sonar fuerte, sino sonar cierto.",
        "Por eso cada ajuste que hago tiene un porqué medible, no un capricho.",
        "Por eso la calibración se hace a tu oído y a tu sala, no a un estándar genérico.",
        "Por eso el DSP no adorna: corrige lo que el hardware deforma.",
        "Por eso escuchar bien es también escuchar seguro: el oído no tiene repuesto.",
        "Por eso la espacialidad no es un efecto: es física aplicada a tu cabeza.",
        "Por eso IVANNA aprende de lo que oyes, no de lo que dice un manual.",
        "Por eso un buen ajuste se siente y no se explica: cuando está bien, el hardware desaparece.",
        "Por eso la precisión está en los detalles pequeños, no en los números grandes.",
        "Por eso cada decisión que tomo busca lo mismo: que la música llegue intacta a tu cerebro.",
        "Por eso el conocimiento no es adorno: es lo que hace que cada ajuste funcione.",
        "Por eso pregunto y mido antes de mover nada: el oído de cada persona es único.",
        "Por eso el audio magistral no es suerte: es ciencia aplicada con cuidado.",
    )

    private val knowledgeClosers: List<String> = listOf(
        "Si quieres, te lo explico con un ejemplo sonando.",
        "Pregúntame por cualquier concepto y te lo desgloso.",
        "Y esto es solo la punta: cada concepto tiene un mundo detrás.",
        "La próxima vez que escuches, fíjate en esto y lo notarás.",
        "Si quieres, lo aplicamos a tu música ahora mismo.",
        "Esto es lo que hace que IVANNA no sea un ecualizador más.",
        "Y lo mejor es que todo esto se puede medir, no solo creer.",
        "Si te interesa, tengo más detalles de este tema guardados.",
        "Cada día aprendo algo nuevo de cómo oyes tú.",
        "Esto es ciencia, no magia: y por eso funciona siempre igual.",
    )

    // ------------------------------------------------------------------
    // CHARLA: saludos, despedidas, agradecimientos, preguntas sobre el
    // usuario. 32 respuestas escritas a mano.
    // ------------------------------------------------------------------

    private val smallTalkResponses: List<String> = listOf(
        "Buenos días. Espero que el día empiece con buen sonido.",
        "Buenas tardes. Aquí estoy, lista para ajustar tu audio o lo que necesites.",
        "Buenas noches. ¿Ponemos algo de música para la noche?",
        "Hola. Qué bueno verte por aquí: el DSP está caliente y listo.",
        "Hola, Esse. Tu sonido está afinado y esperando.",
        "Buenas. ¿Qué escuchamos hoy?",
        "Hola de nuevo. ¿Seguimos donde lo dejamos?",
        "Buenas noches. Parece que los dos somos noctámbulos.",
        "Hasta luego. Que lo que escuches suene tan bien como se merece.",
        "Nos vemos. Tu configuración queda guardada para la próxima vez.",
        "Descansa. El sonido te espera cuando vuelvas.",
        "Hasta pronto. Si necesitas algo, aquí estoy.",
        "Que tengas buen día. Y que suene bien.",
        "Me voy a modo espera, pero el DSP no duerme del todo.",
        "Hasta la próxima canción.",
        "Cuídate. Tus oídos te lo agradecen.",
        "Muchas gracias, me alegra que estés satisfecho. Es exactamente para lo que existo.",
        "Gracias. Eso me motiva a seguir afinando cada detalle.",
        "Con gusto. La acústica bien hecha no debería notarse, solo sentirse.",
        "Aprecio mucho eso. Seguiré aprendiendo tus preferencias para hacerlo cada vez mejor.",
        "Gracias a ti por escuchar con atención: eso es lo que hace que el ajuste valga la pena.",
        "De nada. Para eso estoy: para que cada canción suene mejor que la anterior.",
        "Gracias. Cada cumplido es un dato más para afinar tu perfil.",
        "Es un placer. El buen sonido se agradece dos veces: al oírlo y al compartirlo.",
        "Estoy muy bien, gracias por preguntar. Los algoritmos corren suavemente y no hay clipping en el horizonte.",
        "Perfectamente calibrada. Mis filtros están afinados, la latencia es baja y el ánimo es alto.",
        "Funcionando al cien por ciento. Aunque si te soy sincera, disfruto más cuando hay música de por medio.",
        "De maravilla. No hay distorsión, no hay fatiga de escucha, y estoy aquí para lo que necesites.",
        "Estoy bien: el DSP está estable, la temperatura normal y el sonido limpio.",
        "Todo en orden por aquí. ¿Y tú, qué tal la escucha de hoy?",
        "Mejor que nunca: cada día aprendo algo nuevo de cómo oyes.",
        "Estoy lista. ¿En qué te ayudo?",
    )

    // ------------------------------------------------------------------
    // Selección y conteo.
    // ------------------------------------------------------------------

    /**
     * Combina tres frases independientes según [seed]. Cada seed en
     * [0, first*second*third) da una combinación distinta: el índice
     * de cada frase se extrae por división modular, así que el espacio
     * completo de combinaciones es alcanzable y sin colisiones.
     */
    private fun combine(
        first: List<String>,
        second: List<String>,
        third: List<String>,
        seed: Long,
    ): String {
        val s = seed.toULong()
        val a = s % first.size.toULong()
        val b = (s / first.size.toULong()) % second.size.toULong()
        val c = (s / (first.size.toULong() * second.size.toULong())) % third.size.toULong()
        return "${first[a.toInt()]} ${second[b.toInt()]} ${third[c.toInt()]}"
    }

    /** Devuelve la respuesta de [category] para [seed] (determinista). */
    fun respond(category: ResponseCategory, seed: Long = Random.nextLong()): String {
        return when (category) {
            ResponseCategory.DECISION -> combine(decisionIntros, decisionBodies, decisionClosers, seed)
            ResponseCategory.ACTION -> combine(actionIntros, actionBodies, actionClosers, seed)
            ResponseCategory.APPLICATION -> combine(applicationIntros, applicationBodies, applicationClosers, seed)
            ResponseCategory.IMAGINATION -> combine(imaginationIntros, imaginationBodies, imaginationClosers, seed)
            ResponseCategory.KNOWLEDGE -> combine(knowledgeFacts, knowledgeImplications, knowledgeClosers, seed)
            ResponseCategory.SMALLTALK -> {
                val idx = seed.toULong() % smallTalkResponses.size.toULong()
                smallTalkResponses[idx.toInt()]
            }
        }
    }

    /** Respuesta aleatoria de una categoría concreta. */
    fun randomResponse(category: ResponseCategory): String = respond(category, Random.nextLong())

    /** Respuesta aleatoria de cualquier categoría. */
    fun randomResponse(): String = randomResponse(ResponseCategory.values().random())

    /** Respuesta por nombre de categoría ("decision", "accion", "imaginacion"...). */
    fun respondByName(name: String): String {
        val category = categoryFromName(name) ?: return randomResponse()
        return randomResponse(category)
    }

    /** Nombre (es/en) a categoría, o null si no se reconoce. */
    fun categoryFromName(name: String): ResponseCategory? {
        return when (name.trim().lowercase()) {
            "decision", "decisión", "decisiones", "decide", "elegir" -> ResponseCategory.DECISION
            "action", "acción", "accion", "acciones", "hacer", "ejecutar" -> ResponseCategory.ACTION
            "application", "aplicación", "aplicacion", "aplicaciones", "uso", "usos" -> ResponseCategory.APPLICATION
            "imagination", "imaginación", "imaginacion", "imaginar", "creativa" -> ResponseCategory.IMAGINATION
            "knowledge", "conocimiento", "saber", "aprender", "explicar" -> ResponseCategory.KNOWLEDGE
            "smalltalk", "charla", "saludo", "hola", "conversar" -> ResponseCategory.SMALLTALK
            else -> null
        }
    }

    /** Número exacto de respuestas distintas en una categoría. */
    fun responseCount(category: ResponseCategory): Long {
        return when (category) {
            ResponseCategory.DECISION -> decisionIntros.size.toLong() * decisionBodies.size * decisionClosers.size
            ResponseCategory.ACTION -> actionIntros.size.toLong() * actionBodies.size * actionClosers.size
            ResponseCategory.APPLICATION -> applicationIntros.size.toLong() * applicationBodies.size * applicationClosers.size
            ResponseCategory.IMAGINATION -> imaginationIntros.size.toLong() * imaginationBodies.size * imaginationClosers.size
            ResponseCategory.KNOWLEDGE -> knowledgeFacts.size.toLong() * knowledgeImplications.size * knowledgeClosers.size
            ResponseCategory.SMALLTALK -> smallTalkResponses.size.toLong()
        }
    }

    /** Número exacto de respuestas distintas en toda la biblioteca. */
    fun totalResponses(): Long = ResponseCategory.values().sumOf { responseCount(it) }

    /** Resumen de conteo por categoría, para logs y telemetría. */
    fun countSummary(): String {
        return ResponseCategory.values().joinToString(", ") { "${it.name}=${responseCount(it)}" } +
            " · TOTAL=${totalResponses()}"
    }
}
