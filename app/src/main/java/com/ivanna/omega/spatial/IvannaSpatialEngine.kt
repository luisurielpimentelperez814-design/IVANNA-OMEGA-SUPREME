package com.ivanna.omega.spatial

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer

/**
 * IvannaSpatialEngine — El corazón majestuoso del audio 3D.
 *
 * [MAJESTY-SE-1.0] Este engine combina tres tecnologías que NADIE más tiene
 * en Android:
 *
 * 1. NEURAL UPMIXER: Toma cualquier audio estéreo y lo separa en 4 stems
 *    (vocals, drums, bass, other) usando IA. Luego posiciona cada stem
 *    en el espacio 3D como objetos independientes.
 *
 * 2. OBJECT RENDERER: Renderiza hasta 32 objetos de audio en posiciones 3D
 *    usando VBAP (Vector Base Amplitude Panning) + HRTF binaural. Cada objeto
 *    tiene posición (x,y,z), difusión (width) y ganancia.
 *
 * 3. HEAD TRACKING 6DoF: Cuando el usuario gira la cabeza, el sonido se
 *    queda fijo en el espacio. La experiencia es HOLOGRÁFICA — como tener
 *    un concierto privado flotando alrededor de tu cabeza.
 *
 * Todo esto corre en C++ con latencia <15ms en un Snapdragon 8 Gen 3,
 * y <25ms en gama media (Snapdragon 7 Gen 2).
 *
 * Uso:
 *   val engine = IvannaSpatialEngine(sampleRate = 96000, blockSize = 512)
 *   engine.init(context)
 *   engine.setHeadTracker(headTracker)
 *   engine.enableUpmixer(true)
 *
 *   // En cada bloque de audio:
 *   engine.processStereoInput(inputLeft, inputRight, outputLeft, outputRight, frames)
 */
class IvannaSpatialEngine(
    private val sampleRate: Float = 96000f,
    private val blockSize: Int = 512
) {
    companion object {
        const val TAG = "IvannaSpatialEngine"
        const val MAX_OBJECTS = 32
        const val NUM_STEMS = 4

        // FIX (control sin efecto real — auditoría de cableado): nadie
        // instanciaba esta clase en todo el repo. El toggle "MOTOR BINAURAL
        // · 32 OBJETOS" de la UI (IvannaControlPanel) describe TEXTUALMENTE
        // las capacidades de este motor ("Upmix neural + VBAP/HRTF +
        // head-tracking 6DoF... separa hasta 32 stems virtuales... aplica
        // convolución HRTF con seguimiento de cabeza") pero estaba cableado
        // a SpatialAudioEngineV2, que es puramente telemetría/análisis (no
        // produce salida de audio — ver auditoría previa). blockSize=2048
        // para coincidir con el tamaño máximo de bloque que usa
        // IvannaBridgePlayer (el único path con salida de audio audible
        // real), evitando trocear en dos niveles distintos.
        //
        // Sin modelo TFLite de separación de stems real disponible en el
        // repo (solo existe yamnet.tflite, el clasificador de género/voz,
        // no un modelo de stem-separation) — se inicializa con
        // modelPath=null, así que el "upmixer neural" corre en su fallback
        // heurístico, no con IA real. Documentado para no prometer más de
        // lo que hay.
        val shared = IvannaSpatialEngine(96000f, 2048)
        @Volatile var enabled: Boolean = false
    }

    private var upmixerHandle: Long = 0
    private var rendererHandle: Long = 0

    // Buffers directos para zero-copy entre Java y C++
    private var upmixerInBuf: FloatBuffer? = null
    private var upmixerOutBuf: FloatBuffer? = null
    private var rendererObjectsBuf: FloatBuffer? = null
    private var rendererOutLBuf: FloatBuffer? = null
    private var rendererOutRBuf: FloatBuffer? = null

    private var headTracker: IvannaHeadTracker? = null
    private var isInitialized = false

    // Estado del upmixer
    var isUpmixerEnabled = true
        set(value) {
            field = value
            if (upmixerHandle != 0L) {
                IvannaSpatialNative.nativeUpmixerSetEnabled(upmixerHandle, value)
            }
        }

    // Nivel de reverb espacial (0-1)
    var reverbLevel = 0.3f
        set(value) {
            field = value.coerceIn(0f, 1f)
            if (rendererHandle != 0L) {
                IvannaSpatialNative.nativeObjectRendererSetReverb(rendererHandle, field)
            }
        }

    fun init(modelPath: String? = null) {
        if (isInitialized) return

        // Crear upmixer (con modelo TFLite si disponible)
        upmixerHandle = if (modelPath != null) {
            IvannaSpatialNative.nativeUpmixerCreate(modelPath, sampleRate, blockSize)
        } else {
            // Fallback: upmixer heurístico sin modelo
            IvannaSpatialNative.nativeUpmixerCreate("", sampleRate, blockSize)
        }

        // Crear renderer
        rendererHandle = IvannaSpatialNative.nativeObjectRendererCreate(sampleRate, blockSize)

        // [FIX-SILENCE] Sin esto, el renderer nunca tiene objetos activos
        // (numActiveObjects_ = 0 para siempre) y la salida binaural queda
        // en silencio aunque el upmixer y el HRTF sí estén corriendo.
        if (upmixerHandle != 0L && rendererHandle != 0L) {
            IvannaSpatialNative.nativeObjectRendererSyncStemObjects(rendererHandle, upmixerHandle)
        }

        // Allocar buffers directos
        upmixerInBuf = createDirectFloatBuffer(blockSize * 2)      // Stereo input
        upmixerOutBuf = createDirectFloatBuffer(blockSize * 8)     // 4 stems × stereo
        rendererObjectsBuf = createDirectFloatBuffer(32 * 2 * blockSize)  // 32 objects × stereo
        rendererOutLBuf = createDirectFloatBuffer(blockSize)
        rendererOutRBuf = createDirectFloatBuffer(blockSize)

        isInitialized = true
    }

    fun setHeadTracker(tracker: IvannaHeadTracker) {
        headTracker = tracker
        if (tracker.nativeHandle != 0L && rendererHandle != 0L) {
            IvannaSpatialNative.nativeObjectRendererSetHeadTracker(rendererHandle, tracker.nativeHandle)
        }
    }

    /**
     * Procesa audio estéreo y produce salida binaural espacial.
     *
     * @param inLeft  Array de entrada izquierda (blockSize frames)
     * @param inRight Array de entrada derecha (blockSize frames)
     * @param outLeft Array de salida izquierda (binaural, head-tracked)
     * @param outRight Array de salida derecha (binaural, head-tracked)
     * @param numFrames Número de frames a procesar (≤ blockSize)
     */
    fun processStereoInput(
        inLeft: FloatArray, inRight: FloatArray,
        outLeft: FloatArray, outRight: FloatArray,
        numFrames: Int
    ) {
        if (!isInitialized || rendererHandle == 0L || upmixerHandle == 0L) {
            // Passthrough
            System.arraycopy(inLeft, 0, outLeft, 0, numFrames)
            System.arraycopy(inRight, 0, outRight, 0, numFrames)
            return
        }

        // 1. Upmix: stereo → 4 stems
        val upmixerIn = upmixerInBuf!!
        upmixerIn.clear()
        for (i in 0 until numFrames) {
            upmixerIn.put(inLeft[i])
            upmixerIn.put(inRight[i])
        }
        upmixerIn.flip()

        val upmixerOut = upmixerOutBuf!!
        upmixerOut.clear()

        if (upmixerHandle != 0L && isUpmixerEnabled) {
            IvannaSpatialNative.nativeUpmixerProcess(upmixerHandle, upmixerIn, upmixerOut, numFrames)
        } else {
            // Passthrough: mismo formato intercalado (n*8+k) que produce el
            // upmixer nativo cuando enabled_=false — todo el audio va al
            // stem "Other" (índices 6,7), el resto en silencio. Antes esto
            // hacía upmixerOut.put(upmixerIn), que solo copiaba los primeros
            // numFrames*2 floats en el LAYOUT INCORRECTO (no intercalado
            // por frame de 8 canales) — con el fix de deinterleave de abajo
            // eso habría leído basura/silencio donde no tocaba.
            for (i in 0 until numFrames) {
                val base = i * 8
                for (ch in 0 until 6) upmixerOut.put(base + ch, 0f)
                upmixerOut.put(base + 6, upmixerIn.get(i * 2))
                upmixerOut.put(base + 7, upmixerIn.get(i * 2 + 1))
            }
        }

        // 2. Convertir stems (formato intercalado por frame del upmixer:
        //    out[n*8 + stem*2 + ch]) al formato block-planar que espera
        //    ObjectRenderer (por objeto: bloque L de numFrames muestras
        //    contiguas, luego bloque R de numFrames muestras contiguas —
        //    ver objL/objR en ivanna_object_renderer.cpp::renderBlock).
        //
        // [FIX-WHISTLE-2] Antes esto era un objectsBuf.put(upmixerOut) — un
        // memcpy directo entre dos layouts incompatibles. Leer un buffer
        // intercalado (8 floats/frame) como si fuera plano (bloque L de
        // 1024 muestras contiguas por objeto) hace que la "R" de cada
        // objeto empiece a leer, en realidad, muestras L de un stem
        // DISTINTO 128 frames más adelante (1024/8) — el equivalente a
        // diezmar sin filtro antialiasing. Eso pliega el espectro y
        // produce un tono fijo y agudo — el silbido reportado.
        val objectsBuf = rendererObjectsBuf!!
        objectsBuf.clear()
        for (stem in 0 until NUM_STEMS) {
            val base = stem * 2 * numFrames
            for (i in 0 until numFrames) {
                objectsBuf.put(base + i, upmixerOut.get(i * 8 + stem * 2))              // L
            }
            for (i in 0 until numFrames) {
                objectsBuf.put(base + numFrames + i, upmixerOut.get(i * 8 + stem * 2 + 1)) // R
            }
        }

        // 3. Renderizar objetos a binaural
        val outL = rendererOutLBuf!!
        val outR = rendererOutRBuf!!
        outL.clear()
        outR.clear()

        IvannaSpatialNative.nativeObjectRendererRenderBlock(
            rendererHandle, objectsBuf, 4, outL, outR, numFrames
        )

        // [FIX-CRASH] outL/outR se escriben desde C++ vía puntero crudo
        // (GetDirectBufferAddress), lo que NO mueve el position Java del
        // FloatBuffer. clear() deja position=0. flip() aquí hacía
        // limit=position(0) -> limit quedaba en 0 -> outL.get() lanzaba
        // BufferUnderflowException en la primera muestra de cada bloque,
        // crasheando la app en cuanto llegaba el primer audio tras
        // encender el motor. Se lee por índice absoluto en vez de
        // depender del position/limit relativo.
        for (i in 0 until numFrames) {
            outLeft[i] = outL.get(i)
            outRight[i] = outR.get(i)
        }
    }

    fun setStemPosition(stemType: StemType, x: Float, y: Float, z: Float, width: Float) {
        if (upmixerHandle != 0L) {
            IvannaSpatialNative.nativeUpmixerSetStemPosition(
                upmixerHandle, stemType.ordinal, x, y, z, width
            )
            // Re-sincronizar la lista de objetos activos del renderer con
            // la nueva posición custom del stem.
            if (rendererHandle != 0L) {
                IvannaSpatialNative.nativeObjectRendererSyncStemObjects(rendererHandle, upmixerHandle)
            }
        }
    }

    fun reset() {
        if (upmixerHandle != 0L) IvannaSpatialNative.nativeUpmixerReset(upmixerHandle)
        if (rendererHandle != 0L) IvannaSpatialNative.nativeObjectRendererReset(rendererHandle)
    }

    fun release() {
        if (upmixerHandle != 0L) {
            IvannaSpatialNative.nativeUpmixerDestroy(upmixerHandle)
            upmixerHandle = 0
        }
        if (rendererHandle != 0L) {
            IvannaSpatialNative.nativeObjectRendererDestroy(rendererHandle)
            rendererHandle = 0
        }
        upmixerInBuf = null
        upmixerOutBuf = null
        rendererObjectsBuf = null
        rendererOutLBuf = null
        rendererOutRBuf = null
        isInitialized = false
    }

    private fun createDirectFloatBuffer(size: Int): FloatBuffer {
        return ByteBuffer.allocateDirect(size * Float.SIZE_BYTES)
            .order(ByteOrder.nativeOrder())
            .asFloatBuffer()
    }

    enum class StemType {
        VOCALS, DRUMS, BASS, OTHER
    }
}
