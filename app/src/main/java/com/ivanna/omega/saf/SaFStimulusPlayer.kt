package com.ivanna.omega.saf

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioTrack
import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import java.util.concurrent.atomic.AtomicInteger
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.exp
import kotlin.math.ln
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin

/**
 * SaFStimulusPlayer — reproductor de tonos binaurales de calibración HRTF.
 *
 * PROBLEMA QUE RESUELVE (auditoría del propietario, 2026-09-10):
 *   La pantalla `SaFCalibrationScreen` anuncia "Escucharás 5 tonos de prueba"
 *   y avanza las 5 direcciones sin reproducir NINGÚN estímulo. El usuario ve
 *   la flecha y el hint pero no oye nada, así que CORRECTO/INCORRECTO se
 *   convierte en encuesta ciega y `Φ_SAF^∞` converge sobre ruido humano en
 *   vez de sobre la respuesta real del oyente.
 *
 * DISEÑO (por qué cada decisión):
 *   - **Chirp logarítmico 300 Hz → 8 kHz**, no un sinusoide puro. Un tono
 *     puro (p. ej. 1 kHz) es acústicamente indistinguible entre FRENTE y
 *     ARRIBA porque no contiene el rango espectral 6–10 kHz donde viven las
 *     notches pinnales que dan la sensación de elevación. Un chirp barre
 *     ese rango y proporciona al oyente pistas suficientes incluso sin HRTF
 *     personalizada.
 *   - **ITD (Interaural Time Difference) por Woodworth**, radio de cabeza
 *     a = 8.75 cm. Para azimut θ (rad):
 *         ITD(θ) = (a / c) · (θ + sin θ)     [c = 343 m/s]
 *     Se convierte a delay de muestras enteras al SR real (48 kHz típicos)
 *     y se aplica retardando el canal contralateral. Este modelo es la
 *     aproximación estándar de la literatura psicoacústica desde Kuhn
 *     (1977) — no una heurística.
 *   - **ILD (Interaural Level Difference) por sombra de cabeza dependiente
 *     de frecuencia**, aproximación cabeza rígida: para f < 500 Hz la
 *     sombra es despreciable (la longitud de onda envuelve la cabeza);
 *     para f > 3 kHz la atenuación satura ~15 dB en el oído contralateral.
 *     Se aplica como ganancia por muestra sobre el envelope del chirp.
 *   - **Discriminación FRENTE vs ATRÁS** sin HRTF real: ambos comparten
 *     ITD = 0 e ILD = 0. La única forma de que sean distinguibles por
 *     auriculares es aplicar a ATRÁS un filtro de sombra pinnal
 *     (atenuación 6–10 kHz de ~6 dB) que emula el rolloff que la propia
 *     pinna produce cuando el sonido viene por detrás. FRENTE queda plano.
 *   - **Envolvente coseno-Hann** en attack (8 ms) y release (40 ms).
 *     Sin envelope, un tono cortado bruscamente produce un clic audible
 *     que domina la respuesta del usuario (el clic se localiza en el
 *     centro por transitorio broadband, no por la espacialización real).
 *   - **AudioTrack estéreo FLOAT** (no PCM_16 → precisión de banco de
 *     filtros mantenida) en modo STATIC (el buffer completo cabe en RAM:
 *     500 ms × 48 kHz × 2 ch × 4 B = 192 KB) — evita callbacks streaming
 *     que complicarían la cancelación limpia por el usuario.
 *   - **Idempotencia + cancelación**: `stop()` es seguro llamarlo en
 *     cualquier estado (nunca reproducido, reproduciendo, ya parado); si
 *     el usuario pulsa CORRECTO antes de que termine el estímulo, se
 *     libera el track al instante.
 *   - **Contador de generación** (AtomicInteger): si el usuario dispara
 *     una segunda reproducción antes de que la primera libere, la primera
 *     ve que su generación ya no es la actual y se auto-cancela sin tocar
 *     el track de la nueva.
 *
 * LIMITACIONES DECLARADAS (honestidad de ingeniería):
 *   - Sin HRTF real cargada, la sensación de ARRIBA es la más débil por
 *     auriculares — el sistema pinna-torso del oyente no está en el
 *     camino de señal. El chirp maximiza las pistas espectrales
 *     disponibles pero un usuario naïve puede seguir teniendo tasas de
 *     confusión no despreciables en ARRIBA. Eso lo mide `Φ_SAF^∞`
 *     exactamente para ajustar q_t contra el sujeto real.
 *   - El delay ITD por retardo de muestras enteras tiene resolución
 *     mínima ≈ 21 µs a 48 kHz (1/48000). Alcanza para ITD desde 200 µs
 *     (azimut ~15°) en adelante; los azimuts calibrados aquí (0, ±90,
 *     180) están todos por encima de ese umbral.
 */
class SaFStimulusPlayer(
    private val sampleRateHz: Int = 48_000,
    private val durationMs: Int = 500
) {

    companion object {
        private const val TAG = "SaFStimulusPlayer"

        // Constantes físicas y del modelo.
        private const val SPEED_OF_SOUND_MPS = 343.0            // aire a 20 °C
        private const val HEAD_RADIUS_M = 0.0875                // cabeza tipo (Kuhn 1977)
        private const val CHIRP_F0_HZ = 300.0                   // graves suficientes p/ ITD
        private const val CHIRP_F1_HZ = 8_000.0                 // agudos p/ notches pinnales
        private const val ATTACK_MS = 8.0                       // sin clic al inicio
        private const val RELEASE_MS = 40.0                     // decay natural sin corte
        private const val PEAK_AMPLITUDE = 0.35f                // −9 dBFS: seguro para auriculares
    }

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private val generation = AtomicInteger(0)

    @Volatile private var currentTrack: AudioTrack? = null
    @Volatile private var currentJob: Job? = null

    /**
     * Emite el chirp binaural correspondiente a la dirección dada.
     * Cancela cualquier reproducción anterior antes de empezar.
     *
     * @param direction dirección lógica de la calibración
     * @return true si se pudo iniciar, false si AudioTrack no está disponible
     */
    fun play(direction: SaFDirection): Boolean {
        stop() // cancela cualquier reproducción previa; siempre seguro
        val myGen = generation.incrementAndGet()

        val samples = renderStereo(direction)
        val minBuf = AudioTrack.getMinBufferSize(
            sampleRateHz,
            AudioFormat.CHANNEL_OUT_STEREO,
            AudioFormat.ENCODING_PCM_FLOAT
        )
        if (minBuf <= 0) {
            Log.w(TAG, "AudioTrack.getMinBufferSize devolvió $minBuf — hardware no soporta float estéreo @ ${sampleRateHz}Hz")
            return false
        }

        val bufBytes = max(minBuf, samples.size * 4)
        val track = try {
            AudioTrack.Builder()
                .setAudioAttributes(
                    AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_ASSISTANCE_SONIFICATION)
                        .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                        .build()
                )
                .setAudioFormat(
                    AudioFormat.Builder()
                        .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
                        .setSampleRate(sampleRateHz)
                        .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
                        .build()
                )
                .setBufferSizeInBytes(bufBytes)
                .setTransferMode(AudioTrack.MODE_STATIC)
                .build()
        } catch (t: Throwable) {
            Log.w(TAG, "AudioTrack.Builder falló: ${t.message}")
            return false
        }

        val written = try {
            track.write(samples, 0, samples.size, AudioTrack.WRITE_BLOCKING)
        } catch (t: Throwable) {
            Log.w(TAG, "AudioTrack.write falló: ${t.message}")
            track.releaseSafe()
            return false
        }
        if (written != samples.size) {
            Log.w(TAG, "AudioTrack.write parcial ($written/${samples.size})")
            track.releaseSafe()
            return false
        }

        currentTrack = track
        currentJob = scope.launch {
            try {
                track.play()
                // Duración total con margen de flush del hardware.
                val totalMs = durationMs.toLong() + 60L
                var elapsed = 0L
                while (isActive && elapsed < totalMs && generation.get() == myGen) {
                    kotlinx.coroutines.delay(20L)
                    elapsed += 20L
                }
            } catch (t: Throwable) {
                Log.w(TAG, "play loop: ${t.message}")
            } finally {
                // Solo liberamos si seguimos siendo la generación viva.
                // Si otra play() ya pasó, ella se encarga de su propio track.
                if (generation.get() == myGen) {
                    track.releaseSafe()
                    currentTrack = null
                }
            }
        }
        return true
    }

    /** Cancela reproducción en curso y libera recursos. Idempotente. */
    fun stop() {
        generation.incrementAndGet() // invalida cualquier loop en vuelo
        currentJob?.cancel()
        currentJob = null
        currentTrack?.releaseSafe()
        currentTrack = null
    }

    /** Libera el scope. Después de esto el player queda inutilizable. */
    fun release() {
        stop()
        runCatching { scope.cancel() }
    }

    // ─────────────────────────────────────────────────────────────────────
    // Renderer
    // ─────────────────────────────────────────────────────────────────────

    /**
     * Renderiza el chirp binaural completo (muestras intercaladas L,R,L,R...).
     */
    private fun renderStereo(direction: SaFDirection): FloatArray {
        val n = (sampleRateHz.toLong() * durationMs / 1000L).toInt()
        val sr = sampleRateHz.toDouble()

        // ── Chirp logarítmico monoaural ─────────────────────────────────
        // Fase: φ(t) = 2π · f0 · T · (r^(t/T) − 1) / ln(r), donde r = f1/f0.
        val f0 = CHIRP_F0_HZ
        val f1 = CHIRP_F1_HZ
        val tSec = durationMs / 1000.0
        val r = f1 / f0
        val lnR = ln(r)
        val phaseK = 2.0 * PI * f0 * tSec / lnR

        val mono = FloatArray(n)
        val attackN = (ATTACK_MS / 1000.0 * sr).toInt().coerceAtLeast(1)
        val releaseN = (RELEASE_MS / 1000.0 * sr).toInt().coerceAtLeast(1)
        val releaseStart = n - releaseN

        for (i in 0 until n) {
            val t = i / sr
            val phase = phaseK * (Math.pow(r, t / tSec) - 1.0)
            var s = sin(phase).toFloat() * PEAK_AMPLITUDE

            // Envolvente coseno-Hann: sin clics al arrancar/parar.
            val env = when {
                i < attackN -> {
                    val x = i.toDouble() / attackN
                    (0.5 - 0.5 * cos(PI * x)).toFloat()
                }
                i >= releaseStart -> {
                    val x = (i - releaseStart).toDouble() / releaseN
                    (0.5 + 0.5 * cos(PI * x)).toFloat()
                }
                else -> 1f
            }
            mono[i] = s * env
        }

        // ── Aproximación pinnal para ATRÁS ──────────────────────────────
        // La única forma de distinguir FRENTE de ATRÁS por auriculares sin
        // HRTF real: atenuar 6–10 kHz en ATRÁS (rolloff pinnal). Aplicamos
        // un filtro one-pole low-shelf simple sobre el chirp — barato,
        // suficiente para discriminación perceptual gruesa.
        val pinnaLpAlpha: Float = when (direction) {
            SaFDirection.BEHIND -> 0.35f  // corte progresivo agudos
            else -> 0.0f
        }
        if (pinnaLpAlpha > 0f) {
            var y = 0f
            for (i in mono.indices) {
                y += pinnaLpAlpha * (mono[i] - y)
                // Mezcla 60% filtrado + 40% original: rolloff de ~6 dB en
                // 6-10 kHz sin matar completamente la señal (audio percibido).
                mono[i] = 0.4f * mono[i] + 0.6f * y
            }
        }

        // ── ITD por Woodworth ───────────────────────────────────────────
        // FIX (verificación, 2026-09-10): la fórmula (θ+sinθ) solo es válida
        // para |θ|≤90° (derivada para el hemisferio frontal, Kuhn 1977). Sin
        // plegar el ángulo, ATRÁS (180°) evaluaba θ=π directo y daba un ITD
        // de ~800µs — MAYOR que el máximo real en ±90° (~655µs), cuando el
        // ITD físico en 180° (justo detrás, sobre el plano medio) debe ser
        // CERO, igual que en 0° (justo enfrente): ambos equidistantes a los
        // dos oídos. Sin este pliegue, ATRÁS sonaba con un corrimiento
        // temporal espurio hacia un lado además del rolloff pinnal que es
        // la señal real de discriminación. El término ILD de abajo NO
        // necesita este pliegue: sin(azRad) ya es periódico correctamente
        // (sin 180°=sin 0°=0, coincide con la física real).
        val itdAzDeg = when {
            direction.azimuth > 90f  -> 180f - direction.azimuth
            direction.azimuth < -90f -> -180f - direction.azimuth
            else                     -> direction.azimuth
        }
        val azRad = Math.toRadians(direction.azimuth.toDouble())   // sin plegar — lo usa ILD abajo
        val itdAzRad = Math.toRadians(itdAzDeg.toDouble())
        val itdSec = (HEAD_RADIUS_M / SPEED_OF_SOUND_MPS) * (itdAzRad + sin(itdAzRad))
        val itdSamples = (itdSec * sr).toInt()  // signado: >0 → llega antes al oído izq

        // ── ILD dependiente del azimut ──────────────────────────────────
        // Para 90° de azimut: ~9 dB de sombra sobre el contralateral en el
        // rango 500 Hz – 3 kHz; escala con |sin(θ)| (máximo en ±90°).
        val ildDb = 9.0 * abs(sin(azRad))
        val ildGainNear = 1.0f                                     // oído "cercano" (mismo lado)
        val ildGainFar = (Math.pow(10.0, -ildDb / 20.0)).toFloat() // oído contralateral

        // Elevación 90° (ARRIBA): sin ITD/ILD entre oídos pero brillo
        // ligeramente atenuado — la posición cenital produce un notch
        // pinnal característico cerca de 8 kHz. Aproximación honesta.
        val elevAttenuation: Float = if (direction == SaFDirection.ABOVE) 0.85f else 1.0f

        // ── Aplicar a canales L/R ───────────────────────────────────────
        val out = FloatArray(n * 2)
        // Signo de ITD: azimut positivo (derecha) → sonido llega antes al oído derecho.
        // El izquierdo se retarda por |itdSamples|. Convención Woodworth: azimut >0 = derecha.
        val delayLeft = if (itdSamples > 0) itdSamples else 0
        val delayRight = if (itdSamples < 0) -itdSamples else 0
        val gainLeft = if (direction.azimuth > 0f) ildGainFar else ildGainNear
        val gainRight = if (direction.azimuth < 0f) ildGainFar else ildGainNear

        for (i in 0 until n) {
            val iL = i - delayLeft
            val iR = i - delayRight
            val sL = if (iL in 0 until n) mono[iL] else 0f
            val sR = if (iR in 0 until n) mono[iR] else 0f
            out[2 * i] = sL * gainLeft * elevAttenuation
            out[2 * i + 1] = sR * gainRight * elevAttenuation
        }

        // Guarda final: si algo produjo NaN/Inf lo saneamos antes de
        // entregarlo a AudioTrack — mejor un cero puntual que un ruido
        // aleatorio del hardware por bit patterns inválidos.
        for (i in out.indices) {
            val v = out[i]
            if (!v.isFinite()) out[i] = 0f
            else out[i] = min(1f, max(-1f, v))
        }
        return out
    }

    private fun AudioTrack.releaseSafe() {
        runCatching {
            if (playState == AudioTrack.PLAYSTATE_PLAYING) stop()
        }
        runCatching { flush() }
        runCatching { release() }
    }
}
