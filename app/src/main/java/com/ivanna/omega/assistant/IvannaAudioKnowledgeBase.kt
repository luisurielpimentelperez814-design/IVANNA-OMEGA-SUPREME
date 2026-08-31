package com.ivanna.omega.assistant

/**
 * IvannaAudioKnowledgeBase — conocimiento acústico especializado de IVANNA.
 *
 * No es un chatbot general. Es un KB compacto y preciso sobre:
 *   acústica, DSP, HRTF, SOFA, SAF, RIR, ecualización, psicoacústica,
 *   fatiga auditiva, loudness, compresión, espacialidad binaural.
 *
 * Uso principal:
 *   - Enriquecer respuestas habladas con una frase explicativa honesta.
 *   - Responder preguntas técnicas del modo experto.
 *   - Dar contexto al usuario de POR QUÉ IVANNA tomó una decisión.
 */
object IvannaAudioKnowledgeBase {

    // ── Snippets por intención (enriquecen la respuesta TTS) ─────────────────

    fun snippetFor(intent: IvannaLanguageCore.AcousticIntent): String = when (intent) {
        IvannaLanguageCore.AcousticIntent.VOICE_CLARITY ->
            "La claridad vocal se logra realzando la banda de presencia entre 2 y 4 kHz."
        IvannaLanguageCore.AcousticIntent.DIALOG_ENHANCEMENT ->
            "Los diálogos mejoran reduciendo la difusión del campo reverberante y centrando la imagen estéreo."
        IvannaLanguageCore.AcousticIntent.MOVIE_IMMERSION ->
            "El modo cine amplía el escenario espacial y ancla las voces en el centro, como en una sala Dolby."
        IvannaLanguageCore.AcousticIntent.MUSIC_FULLNESS ->
            "El perfil de música añade cuerpo en la región media-grave, donde los instrumentos tienen su fundamental."
        IvannaLanguageCore.AcousticIntent.CONCERT_LIVE ->
            "La convolución con una respuesta de sala real replica las reflexiones tempranas y el RT60 del espacio."
        IvannaLanguageCore.AcousticIntent.SPATIAL_EXPANSION ->
            "La espacialidad binaural usa HRTFs del dataset CIPIC para reproducir pistas de localización ITD e ILD."
        IvannaLanguageCore.AcousticIntent.LISTENING_FATIGUE ->
            "La fatiga auditiva se alivia reduciendo la energía en los rangos de 2–5 kHz, donde el oído es más sensible."
        IvannaLanguageCore.AcousticIntent.GENTLE_MODE ->
            "El modo suave reduce la curva de excitación armónica y baja el umbral del compresor."
        IvannaLanguageCore.AcousticIntent.BASS_BOOST ->
            "El refuerzo de graves actúa en la banda shelf por debajo de 200 Hz, con compensación de headroom automática."
        IvannaLanguageCore.AcousticIntent.TREBLE_REDUCE ->
            "La reducción de agudos aplica un shelf a 8 kHz para evitar sibilancia y fatiga en escucha prolongada."
        IvannaLanguageCore.AcousticIntent.FLAT_NEUTRAL ->
            "El perfil neutro desactiva toda coloración: EQ plano, sin espacialidad añadida y sin compresor activo."
        else -> ""
    }

    // ── Respuestas a preguntas técnicas ──────────────────────────────────────

    fun answerTechnical(question: String): String? {
        val q = question.lowercase()
        return when {
            q.contains("hrtf") && q.contains("qué") || q.contains("hrtf") && q.contains("que") ->
                "HRTF son las siglas de Head-Related Transfer Function: la función que describe cómo tu cabeza, orejas y torso transforman el sonido antes de que llegue al tímpano. IVANNA usa perfiles del dataset CIPIC medidos en sujetos reales."
            q.contains("sofa") ->
                "SOFA (Spatially Oriented Format for Acoustics) es el formato estándar AES69 para almacenar HRTFs y respuestas de sala. IVANNA convierte los archivos SOFA a formato IHR1 optimizado para el pipeline de convolución en tiempo real."
            q.contains("rir") ->
                "RIR (Room Impulse Response) es la respuesta acústica de una sala ante un impulso: captura las reflexiones, el tiempo de reverberación y la difusión de ese espacio. IVANNA tiene 200 RIRs grabadas en salas reales."
            q.contains("saf") ->
                "SAF (Spatial Audio Framework) es la capa de IVANNA que gestiona la convolución binaural en tiempo real usando overlap-save FFT, con crossfade espectral de 32 bloques para cambios sin cortes audibles."
            q.contains("eq") || q.contains("ecualizador") ->
                "IVANNA usa un EQ paramétrico de 8 bandas biquad IIR en doble precisión para evitar acumulación de error numérico en los estados del filtro."
            q.contains("compresor") || q.contains("compresión") ->
                "El compresor de IVANNA opera en side-chain con filtrado de paso alto en 80 Hz para evitar que los graves afecten la decisión de compresión, con ataque de 5–31 ms y release adaptativo."
            q.contains("latencia") ->
                "La latencia DSP de IVANNA se mide con 100 corridas de JNI CLOCK_MONOTONIC. En modo daemon RT sobre Magisk, la latencia mediana es menor a 3 ms."
            q.contains("tinyml") ->
                "IVANNA usa TinyML INT8 para la clasificación de contenido en 4 clases (voz, música, bajos, silencio) directamente en el SoC sin enviar audio a ningún servidor."
            q.contains("fatiga") ->
                "La fatiga auditiva temporal ocurre por exposición prolongada a niveles altos, especialmente en la banda de 2–5 kHz donde el oído tiene mayor sensibilidad. IVANNA registra el número de veces que reportas fatiga y sugiere el modo gentle proactivamente."
            else -> null
        }
    }

    // ── Glosario compacto (para el panel experto) ─────────────────────────────

    val glossary: List<Pair<String, String>> = listOf(
        "HRTF"    to "Head-Related Transfer Function — perfil de cómo escucha una persona.",
        "SOFA"    to "Spatially Oriented Format for Acoustics — formato estándar AES69 para HRTFs.",
        "RIR"     to "Room Impulse Response — respuesta acústica real de una sala.",
        "SAF"     to "Spatial Audio Framework — convolución binaural en tiempo real.",
        "ITD"     to "Interaural Time Difference — diferencia de tiempo entre oído izquierdo y derecho.",
        "ILD"     to "Interaural Level Difference — diferencia de nivel entre oídos.",
        "RT60"    to "Tiempo en que la reverberación decae 60 dB — define el tamaño percibido de la sala.",
        "LUFS"    to "Loudness Units Full Scale — medida de loudness integrado (estándar streaming).",
        "GR"      to "Gain Reduction — cuántos dB está reduciendo el compresor.",
        "FFT"     to "Fast Fourier Transform — algoritmo que convierte audio de tiempo a frecuencia.",
        "IIR"     to "Infinite Impulse Response — tipo de filtro digital con retroalimentación.",
        "CIPIC"   to "Base de datos de HRTFs del UC Davis — 45 sujetos reales + maniquí KEMAR.",
        "XRun"    to "Buffer underrun o overrun — cuando el sistema no procesa audio a tiempo.",
        "TinyML"  to "Machine Learning optimizado para SoCs embebidos sin necesidad de GPU ni nube.",
    )
}
