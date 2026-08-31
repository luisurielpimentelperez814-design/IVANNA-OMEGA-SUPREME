package com.ivanna.omega.assistant

import android.util.Log
import com.ivanna.omega.audio.IvannaEffectProfile

/**
 * IvannaMusicalIntentEngine — capa de comprensión de intención musical.
 *
 * Traduce lenguaje musical humano ("épico", "como Abbey Road", "vinilo premium",
 * "más cinematográfico") directamente a parámetros DSP medibles y perfiles
 * IvannaEffectProfile. Complementa a IvannaLanguageCore sin reemplazarlo:
 * actúa cuando la intención acústica ya es MUSICAL_INTENT, y produce un
 * MusicalPreset con toda la información necesaria para la ejecución y la
 * explicación hablada.
 *
 * No inventa APIs: usa solo IvannaEffectProfile (ya existente) y los
 * comandos que VoiceController.executeCommand() ya acepta.
 *
 * Hilo seguro: object stateless, todos los métodos son puros.
 */
object IvannaMusicalIntentEngine {

    private const val TAG = "IvannaMusicalIntent"

    // ── Tipo de resultado ────────────────────────────────────────────────────

    data class MusicalPreset(
        val name: String,
        val profile: IvannaEffectProfile,
        val extraCommand: String? = null,      // comando adicional VoiceController
        val spatialBoost: Float = 0f,          // 0..1 multiplicador adicional de espacialidad
        val explanation: String,               // lo que IVANNA dice al usuario
        val technicalDetail: String            // resumen técnico para reportes
    )

    // ── Presets musicales canónicos ──────────────────────────────────────────

    /**
     * Épico — amplia dinámica, graves con autoridad, presencia dramática.
     * "Frankenstein de Edgar Winter" pedido como magistral encajaría aquí.
     */
    private val EPIC = IvannaEffectProfile(
        eqBands = intArrayOf(260, 220, 140, 60, -20, 20, 100, 200, 240, 200),
        bassStrength = 620, virtualizerStrength = 680, loudnessGainMb = 180,
        compThresholdDb = -13f, compRatio = 3.6f
    )

    /**
     * Abbey Road — calidez de estudio analógico vintage, compresión suave,
     * mids telefónicos pero ricos, imagen estéreo moderada y precisa.
     */
    private val ABBEY_ROAD = IvannaEffectProfile(
        eqBands = intArrayOf(200, 240, 180, 120, 60, 20, 40, 80, 60, 20),
        bassStrength = 360, virtualizerStrength = 220, loudnessGainMb = 140,
        compThresholdDb = -18f, compRatio = 2.4f
    )

    /**
     * Vinilo premium — respuesta de frecuencia con roll-off natural en altas,
     * armónicos pares acentuados, presencia media cálida, sin frialdad digital.
     */
    private val VINYL_PREMIUM = IvannaEffectProfile(
        eqBands = intArrayOf(240, 200, 160, 120, 80, 40, 0, -40, -100, -180),
        bassStrength = 400, virtualizerStrength = 180, loudnessGainMb = 100,
        compThresholdDb = -20f, compRatio = 2.0f
    )

    /**
     * Concierto masivo — escena enorme, reverberación de estadio, impacto físico
     * de los graves, imagen stereo extrema.
     */
    private val CONCERT_MASSIVE = IvannaEffectProfile(
        eqBands = intArrayOf(300, 260, 180, 80, -20, 0, 60, 140, 180, 160),
        bassStrength = 700, virtualizerStrength = 900, loudnessGainMb = 240,
        compThresholdDb = -10f, compRatio = 4.5f
    )

    /**
     * Cinematográfico — voces centradas con poder, graves profundos para impacto
     * explosivo, imagen envolvente para el efecto de sala de cine.
     */
    private val CINEMATIC = IvannaEffectProfile(
        eqBands = intArrayOf(280, 220, 140, 40, 0, 40, 100, 160, 180, 140),
        bassStrength = 500, virtualizerStrength = 760, loudnessGainMb = 200,
        compThresholdDb = -14f, compRatio = 3.8f
    )

    /**
     * Analógico — saturación armónica suave (tube-like), graves redondos,
     * mids ricos, altas sin aspereza digital.
     */
    private val ANALOG_WARM = IvannaEffectProfile(
        eqBands = intArrayOf(180, 200, 160, 100, 60, 20, 0, -20, -60, -120),
        bassStrength = 380, virtualizerStrength = 160, loudnessGainMb = 120,
        compThresholdDb = -22f, compRatio = 1.8f
    )

    /**
     * Estudio profesional — referencia neutral con ligera mejora de claridad,
     * compresión mínima, imagen estéreo natural, sin coloración.
     */
    private val STUDIO_PRO = IvannaEffectProfile(
        eqBands = intArrayOf(20, 10, 0, 0, 0, 0, 20, 30, 20, 10),
        bassStrength = 60, virtualizerStrength = 80, loudnessGainMb = 0,
        compThresholdDb = -24f, compRatio = 1.4f
    )

    /**
     * Análisis microdetalle — separación instrumental máxima, armónicos
     * realzados, claridad extrema, compresión mínima para preservar transitorios.
     */
    private val MICRO_DETAIL = IvannaEffectProfile(
        eqBands = intArrayOf(0, 0, 20, 40, 20, 40, 100, 180, 220, 200),
        bassStrength = 100, virtualizerStrength = 400, loudnessGainMb = 60,
        compThresholdDb = -28f, compRatio = 1.2f
    )

    /**
     * Escenario próximo — como estar en la primera fila: voces y graves directos,
     * reverberación corta, imagen estéreo moderada y realista.
     */
    private val FRONT_ROW = IvannaEffectProfile(
        eqBands = intArrayOf(160, 140, 100, 60, 20, 40, 120, 200, 200, 160),
        bassStrength = 440, virtualizerStrength = 480, loudnessGainMb = 160,
        compThresholdDb = -15f, compRatio = 3.0f
    )

    // ── Banco de frases → presets ────────────────────────────────────────────

    /**
     * Intenta identificar un preset musical en [rawText].
     * Devuelve null si no hay intención musical reconocible.
     */
    fun detect(rawText: String): MusicalPreset? {
        val t = rawText.lowercase().trim()

        // Épico / magistral / poderoso
        if (hits(t, "épico", "epico", "magistral", "poderoso", "impactante",
                  "épica", "epica", "potente", "grandioso", "brutal", "demoledor",
                  "apoteósico", "apoteosico")) {
            return MusicalPreset(
                name = "ÉPICO",
                profile = EPIC,
                extraCommand = "concert_mode",
                spatialBoost = 0.32f,
                explanation = "He creado un perfil épico: graves con autoridad, escena ampliada " +
                    "y presencia dramática. El modo concierto añade la reverberación del escenario.",
                technicalDetail = "EQ épico, concierto activo, espacialidad +32%, compresión agresiva"
            )
        }

        // Abbey Road / Beatles / clásico británico
        if (hits(t, "abbey road", "beatles", "vintage británico", "vintage britanico",
                  "estudio vintage", "años 60", "anos 60", "clásico inglés", "clasico ingles")) {
            return MusicalPreset(
                name = "ABBEY ROAD",
                profile = ABBEY_ROAD,
                spatialBoost = 0f,
                explanation = "He aplicado el perfil Abbey Road: calidez analógica de los estudios " +
                    "de los 60, compresión suave y graves redondos. Como escuchar en cinta de 4 pistas.",
                technicalDetail = "Curva Abbey Road, graves redondos, compresión suave -18dB/2.4:1"
            )
        }

        // Vinilo / tocadiscos / LP / análogo vintage
        if (hits(t, "vinilo", "vinyl", "tocadiscos", "lp", "como disco",
                  "analógico premium", "analogico premium", "vinil premium")) {
            return MusicalPreset(
                name = "VINILO PREMIUM",
                profile = VINYL_PREMIUM,
                spatialBoost = 0f,
                explanation = "He activado el perfil vinilo: roll-off natural en las altas frecuencias, " +
                    "calidez armónica y la presencia media de un tocadiscos de alta gama.",
                technicalDetail = "Roll-off suave >8kHz, armónicos pares, compresión analógica"
            )
        }

        // Concierto masivo / estadio / festival
        if (hits(t, "concierto gigantesco", "estadio", "festival", "arena", "multitud",
                  "concierto masivo", "wembley", "coachella", "lollapalooza",
                  "sala grande", "concierto en vivo")) {
            return MusicalPreset(
                name = "CONCIERTO MASIVO",
                profile = CONCERT_MASSIVE,
                extraCommand = "concert_mode",
                spatialBoost = 0.5f,
                explanation = "He simulado un concierto masivo: graves físicos, escena de estadio, " +
                    "espacialidad extrema y la energía de estar en el campo.",
                technicalDetail = "Virtualizer máximo, concierto ON, espacialidad +50%, sub-bass"
            )
        }

        // Cinematográfico / película / cine / banda sonora
        if (hits(t, "cinematográfico", "cinematografico", "banda sonora", "banda original",
                  "película", "pelicula", "cine épico", "cine epico", "soundtrack",
                  "orquestal", "hans zimmer", "john williams", "nolan")) {
            return MusicalPreset(
                name = "CINEMATOGRÁFICO",
                profile = CINEMATIC,
                spatialBoost = 0.4f,
                explanation = "He activado el perfil cinematográfico: graves profundos para el impacto " +
                    "de la sala IMAX, voces centradas con poder y una imagen envolvente de 270 grados.",
                technicalDetail = "EQ cinematográfico, espacialidad +40%, graves sub reforzados"
            )
        }

        // Analógico / válvulas / tube / vintage warm
        if (hits(t, "analógico", "analogico", "válvulas", "valvulas", "tube",
                  "cinta", "tape", "vintage cálido", "vintage calido",
                  "warmth", "saturación suave", "saturacion suave")) {
            return MusicalPreset(
                name = "ANALÓGICO",
                profile = ANALOG_WARM,
                spatialBoost = 0f,
                explanation = "He activado el perfil analógico: calor de válvulas, compresión " +
                    "natural de cinta y los armónicos pares que el digital pierde.",
                technicalDetail = "Curva analógica, saturación armónica, roll-off >10kHz"
            )
        }

        // Estudio profesional / mezcla de referencia / mixing
        if (hits(t, "estudio profesional", "professional studio", "mezcla de referencia",
                  "mixing", "mastering", "referencia", "monitor de estudio",
                  "crea una configuración de estudio", "como en estudio")) {
            return MusicalPreset(
                name = "ESTUDIO PRO",
                profile = STUDIO_PRO,
                spatialBoost = 0f,
                explanation = "He creado una configuración de estudio profesional: respuesta " +
                    "casi plana, compresión mínima y la imagen estéreo que un ingeniero de mezcla " +
                    "necesita para tomar decisiones honestas.",
                technicalDetail = "EQ casi plano ±20mB, compresión -24dB/1.4:1, virtualizer bajo"
            )
        }

        // Microdetalle / detalles ocultos / separación
        if (hits(t, "quiero escuchar detalles", "detalles que no escucho",
                  "microdetalle", "micro detalle", "más detalle", "mas detalle",
                  "separación instrumental", "separacion instrumental",
                  "detalles ocultos", "análisis", "analisis fino")) {
            return MusicalPreset(
                name = "MICRODETALLE",
                profile = MICRO_DETAIL,
                spatialBoost = 0.2f,
                explanation = "He activado el modo de microdetalle: compresión mínima para " +
                    "preservar los transitorios, armónicos realzados y separación instrumental " +
                    "máxima. Escucharás cosas que normalmente pasan inadvertidas.",
                technicalDetail = "Compresión -28dB/1.2:1, presencia +180mB, separación máxima"
            )
        }

        // Primera fila / escenario próximo / cerca del artista
        if (hits(t, "primera fila", "cerca del escenario", "más cerca del escenario",
                  "mas cerca", "primera planta", "frente al escenario",
                  "como si estuviera ahí", "escenario próximo", "escenario proximo")) {
            return MusicalPreset(
                name = "PRIMERA FILA",
                profile = FRONT_ROW,
                spatialBoost = 0.25f,
                explanation = "He simulado el sonido de primera fila: directo, con peso " +
                    "en los graves y sin el eco de las paredes. El artista está frente a ti.",
                technicalDetail = "Reverberación corta, imagen estéreo realista, presencia alta"
            )
        }

        // Más cálido
        if (hits(t, "más cálido", "mas calido", "más calidez", "mas calidez",
                  "más warm", "menos frío", "menos frio", "suaviza los agudos",
                  "los altos duelen", "muy brillante", "muy agudo")) {
            return MusicalPreset(
                name = "CÁLIDO",
                profile = IvannaEffectProfile.WARM,
                spatialBoost = 0f,
                explanation = "He suavizado el espectro hacia la calidez: realzados los " +
                    "graves medios, reducida la agresividad de las frecuencias altas.",
                technicalDetail = "Curva Baxandall cálida, agudos suavizados, graves medios +180mB"
            )
        }

        // Más potente / más punch / más fuerza
        if (hits(t, "más potente", "mas potente", "más punch", "mas punch",
                  "más impacto", "mas impacto", "más fuerza", "mas fuerza",
                  "más energía", "mas energia", "más agresivo", "mas agresivo")) {
            return MusicalPreset(
                name = "PUNCH",
                profile = IvannaEffectProfile.PUNCH,
                spatialBoost = 0f,
                explanation = "He activado el modo Punch: sub-bass con autoridad, compresión " +
                    "agresiva para ese golpe físico y presencia que corta.",
                technicalDetail = "Sub-bass +280mB, compresor -12dB/4.0:1, loudness +300mB"
            )
        }

        // Espacial / más amplio / más ancho
        if (hits(t, "más espacial", "mas espacial", "más amplio", "mas amplio",
                  "más ancho", "mas ancho", "abre el sonido", "más espacio",
                  "mas espacio", "3d", "surround")) {
            return MusicalPreset(
                name = "ESPACIAL",
                profile = IvannaEffectProfile.SPATIAL,
                spatialBoost = 0.35f,
                explanation = "He ampliado la escena sonora: imagen estéreo extendida, " +
                    "presencia posicional realzada y el aire que faltaba alrededor de los instrumentos.",
                technicalDetail = "Virtualizer +720, espacialidad +35%, presencia 2-4kHz +200mB"
            )
        }

        return null
    }

    /**
     * Devuelve los presets disponibles como lista legible — para la función
     * "muéstrame qué puedes hacer" o el reporte de IVANNA.
     */
    fun availablePresetsDescription(): String =
        "Puedo crear estos perfiles musicales: Épico, Abbey Road, Vinilo Premium, " +
        "Concierto Masivo, Cinematográfico, Analógico, Estudio Profesional, " +
        "Microdetalle, Primera Fila, Cálido, Punch y Espacial."

    private fun hits(text: String, vararg needles: String): Boolean =
        needles.any { text.contains(it) }
}
