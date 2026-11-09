package com.ivanna.omega.audio

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import android.os.Process
import android.util.Log
import androidx.core.content.ContextCompat
import com.ivanna.omega.core.IvannaNativeLib

/**
 * RealAcousticSensor — sensor acústico REAL por micrófono del teléfono
 * (v2.5.369), Ruta A. Android 13+.
 *
 * EL TELÉFONO ES EL SENSOR. Mientras la Ruta A procesa audio en la app,
 * este objeto captura la salida acústica real de los altavoces en la
 * habitación/cabina con el micrófono del teléfono y alimenta el sensor
 * nativo (RealAcousticSensor, C++), que mide por banda ERB la
 * transferencia real altavoz+habitación. El bucle cerrado (corrección de
 * sala en vivo, RoomCorrectionEq en nativeProcessBlock) usa esa medición
 * para corregir el balance tonal. Nada simulado: cada número sale del
 * micrófono. Sin hardware externo, sin compras: solo el teléfono
 * (Android 13+). El Moto G85 es el dispositivo de desarrollo/prueba de
 * referencia, pero el sensor es genérico: AudioRecord estándar.
 *
 * Diferencia con RoomMeasurement: esa es una medición de UNA VEZ (barrido
 * de tonos de ~35 s que diseña una corrección fija). Esta es CONTINUA y
 * EN VIVO: mide mientras suena música normal y corrige en tiempo real.
 *
 * AudioRecord: UNPROCESSED si el dispositivo lo soporta (mic crudo, sin
 * procesado — ideal para medir), si no MIC. PCM_FLOAT mono a 48 kHz,
 * mismo patrón que RoomMeasurement y PlaybackCaptureService. La
 * referencia (lo que sale al altavoz) NO pasa por aquí: la empuja el hilo
 * de audio nativo (nativeProcessBlock) al anillo SPSC del sensor.
 *
 * Permiso: RECORD_AUDIO (ya está en el manifest). start() devuelve false
 * si no está concedido; la UI debe pedirlo en runtime antes de llamar.
 *
 * El bucle cerrado se activa SOLO con [setRoomCorrectionEnabled(true)],
 * acción explícita del usuario (como el bucle neuro). Apagado por
 * defecto: el sensor puede medir sin corregir, y la corrección nunca se
 * aplica sin que el usuario la encienda.
 *
 * onFrame: listener opcional, invocado (hilo de captura) cada vez que el
 * sensor publica un frame nuevo. Es el gancho para el Hiper-Operador Φ:
 * cuando aterrice, la app lo registra para llamar a su setSensorFrame
 * con el frame (mag[24], err[24], coh[24], level, valid). La corrección
 * de sala en vivo NO depende de este listener (corre en C++ en
 * nativeProcessBlock); el listener es solo el transporte al operador.
 */
object RealAcousticSensor {
    private const val TAG = "RealAcousticSensor"
    const val SAMPLE_RATE = 48_000
    private const val READ_CHUNK = 2048

    /** [0..23] mag, [24..47] err, [48..71] coh, [72] level, [73] valid. */

    @Volatile
    var isRunning: Boolean = false
        private set

    @Volatile
    var audioSourceUsed: String = "none"
        private set

    /** Listener de frames nuevos (gancho para el Hiper-Operador Φ).
     *  El array es FloatArray[74]: [0..23] mag, [24..47] err, [48..71]
     *  coh, [72] level, [73] valid (1/0). */
    @Volatile
    var onFrame: ((FloatArray) -> Unit)? = null

    @Volatile
    private var captureThread: Thread? = null
    @Volatile
    private var recorder: AudioRecord? = null
    @Volatile
    private var stopRequested: Boolean = false

    /**
     * Arranca la captura del micrófono y el sensor nativo. Acción
     * explícita del usuario. Devuelve false si no hay permiso
     * RECORD_AUDIO, si no se pudo abrir el micrófono o si ya estaba
     * corriendo (en ese caso no hace nada y devuelve true).
     */
    fun start(context: Context): Boolean {
        if (isRunning) return true
        if (ContextCompat.checkSelfPermission(context, Manifest.permission.RECORD_AUDIO) !=
            PackageManager.PERMISSION_GRANTED) {
            Log.w(TAG, "start: sin permiso RECORD_AUDIO; pedirlo en runtime antes de llamar.")
            return false
        }
        val minBuf = AudioRecord.getMinBufferSize(
            SAMPLE_RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_FLOAT
        ).coerceAtLeast(READ_CHUNK * 4)
        var recorder: AudioRecord? = null
        var source = "UNPROCESSED"
        try {
            recorder = AudioRecord(
                MediaRecorder.AudioSource.UNPROCESSED, SAMPLE_RATE,
                AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_FLOAT, minBuf
            )
        } catch (e: Exception) {
            Log.w(TAG, "UNPROCESSED no disponible: $e")
            recorder = null
        }
        if (recorder == null || recorder.state != AudioRecord.STATE_INITIALIZED) {
            recorder?.release()
            source = "MIC"
            try {
                recorder = AudioRecord(
                    MediaRecorder.AudioSource.MIC, SAMPLE_RATE,
                    AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_FLOAT, minBuf
                )
            } catch (e: Exception) {
                Log.e(TAG, "MIC tampoco disponible: $e")
                recorder = null
            }
        }
        val rcd: AudioRecord = recorder ?: return false
        if (rcd.state != AudioRecord.STATE_INITIALIZED) {
            rcd.release()
            Log.e(TAG, "start: AudioRecord no inicializado")
            return false
        }
        if (!IvannaNativeLib.guardedNative(false) {
                IvannaNativeLib.nativeSensorInit(SAMPLE_RATE)
            }) {
            rcd.release()
            Log.e(TAG, "start: nativeSensorInit falló")
            return false
        }
        audioSourceUsed = source
        stopRequested = false
        this.recorder = rcd
        val thread = Thread({
            Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_AUDIO)
            val buf = FloatArray(READ_CHUNK)
            var lastIndex = -1L
            try {
                rcd.startRecording()
                Log.i(TAG, "Captura de micrófono en vivo ($source @ $SAMPLE_RATE Hz)")
                while (!stopRequested) {
                    val read = rcd.read(buf, 0, READ_CHUNK, AudioRecord.READ_BLOCKING)
                    if (read < 0) {
                        Log.w(TAG, "AudioRecord error $read — saliendo")
                        break
                    }
                    if (read == 0) continue
                    IvannaNativeLib.guardedNative(false) {
                        IvannaNativeLib.nativeSensorFeedMic(buf, read)
                    }
                    // Frame nuevo → avisar al listener (gancho al operador Φ).
                    val listener = onFrame
                    if (listener != null) {
                        val idx = IvannaNativeLib.guardedNative(-1L) {
                            IvannaNativeLib.nativeSensorFrameIndex()
                        }
                        if (idx > lastIndex) {
                            lastIndex = idx
                            val frame = IvannaNativeLib.guardedNative(FloatArray(0)) {
                                IvannaNativeLib.nativeSensorLatestFrame()
                            }
                            if (frame.size == 74) listener(frame)
                        }
                    }
                }
            } catch (e: Exception) {
                Log.e(TAG, "Hilo de captura: $e", e)
            } finally {
                try { rcd.stop() } catch (_: Exception) {}
                rcd.release()
                this.recorder = null
                isRunning = false
                captureThread = null
                Log.i(TAG, "Captura de micrófono detenida")
            }
        }, "IvannaAcousticSensor")
        captureThread = thread
        isRunning = true
        thread.start()
        return true
    }

    /** Detiene la captura. La corrección en vivo se desvanece sola
     *  (el sensor deja de publicar frames válidos). Parar el recorder
     *  desbloquea el read bloqueante del hilo de captura. */
    fun stop() {
        stopRequested = true
        try { recorder?.stop() } catch (_: Exception) {}
        captureThread?.join(1000)
    }

    /** Objetivo por banda (forma deseada, dB, plano = 0). FloatArray[24]. */
    fun setTarget(targetDb: FloatArray): Boolean {
        if (targetDb.size != 24) return false
        return IvannaNativeLib.guardedNative(false) {
            IvannaNativeLib.nativeSensorSetTarget(targetDb)
        }
    }

    /** Preset élite + control de graves (target de corrección).
     *  preset: 0 Harman 2018 (default), 1 Harman élite-lean, 2 campo
     *  difuso H&M, 3 altavoces en sala. bassTrimDb: ±3 dB. */
    fun setEliteTarget(preset: Int, bassTrimDb: Float): Boolean =
        IvannaNativeLib.guardedNative(false) {
            IvannaNativeLib.nativeSensorSetEliteTarget(preset, bassTrimDb)
        }

    /** Control de graves ±3 dB sobre el preset vigente. */
    fun setBassTrimDb(bassTrimDb: Float): Boolean =
        IvannaNativeLib.guardedNative(false) {
            IvannaNativeLib.nativeSensorSetBassTrimDb(bassTrimDb)
        }

    /** Preset vigente del sensor (0..3, ver setEliteTarget). */
    fun targetKind(): Int = IvannaNativeLib.guardedNative(0) {
        IvannaNativeLib.nativeSensorTargetKind()
    }

    /** Último frame: [0..23] mag, [24..47] err, [48..71] coh, [72] level,
     *  [73] valid (1/0). Vacío si el nativo no está cargado. */
    fun latestFrame(): FloatArray = IvannaNativeLib.guardedNative(FloatArray(0)) {
        IvannaNativeLib.nativeSensorLatestFrame()
    }

    fun frameIndex(): Long = IvannaNativeLib.guardedNative(-1L) {
        IvannaNativeLib.nativeSensorFrameIndex()
    }

    /** [0] enabled, [1] amount, [2] frame válido, [3] coherencia media. */
    fun roomCorrectionStatus(): FloatArray =
        IvannaNativeLib.guardedNative(floatArrayOf(0f, 0f, 0f, 0f)) {
            IvannaNativeLib.nativeSensorRoomCorrectionStatus()
        }

    /** Activa/desactiva el bucle cerrado de corrección de sala en vivo
     *  (Ruta A). Acción explícita del usuario. Apagado por defecto. */
    fun setRoomCorrectionEnabled(enabled: Boolean) {
        IvannaNativeLib.guardedNative(Unit) {
            IvannaNativeLib.nativeSensorRoomCorrectionSetEnabled(enabled)
        }
    }

    /** Cantidad de corrección 0..1 (0 = sin corrección, 1 = total). */
    fun setRoomCorrectionAmount(amount: Float) {
        IvannaNativeLib.guardedNative(Unit) {
            IvannaNativeLib.nativeSensorRoomCorrectionSetAmount(amount)
        }
    }
}
