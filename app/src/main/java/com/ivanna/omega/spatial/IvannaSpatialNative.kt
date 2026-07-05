package com.ivanna.omega.spatial

/**
 * IvannaSpatialNative — Declaración JNI del motor espacial majestuoso.
 *
 * [MAJESTY-KOTLIN-1.0] Esta clase expone al mundo Java/Kotlin el poder de:
 *   - Head Tracking 6DoF (orientación de cabeza en tiempo real)
 *   - Object-Based Renderer (32 objetos 3D simultáneos)
 *   - Neural Upmixer (separación AI de stems + spatialización)
 *
 * Todo el procesamiento ocurre en C++ con zero-allocation y lock-free.
 */
object IvannaSpatialNative {
    init {
        System.loadLibrary("ivanna_omega")
    }

    // HeadTracker
    external fun nativeHeadTrackerCreate(): Long
    external fun nativeHeadTrackerDestroy(handle: Long)
    external fun nativeHeadTrackerUpdate(handle: Long, x: Float, y: Float, z: Float, w: Float, timestampMs: Float)
    external fun nativeHeadTrackerReset(handle: Long)

    // ObjectRenderer
    external fun nativeObjectRendererCreate(sampleRate: Float, blockSize: Int): Long
    external fun nativeObjectRendererDestroy(handle: Long)
    external fun nativeObjectRendererSetHeadTracker(rendererHandle: Long, trackerHandle: Long)
    external fun nativeObjectRendererSetReverb(handle: Long, level: Float)
    external fun nativeObjectRendererRenderBlock(
        handle: Long, objectsBuffer: java.nio.FloatBuffer, numObjects: Int,
        outLeftBuffer: java.nio.FloatBuffer, outRightBuffer: java.nio.FloatBuffer, numFrames: Int
    )
    external fun nativeObjectRendererReset(handle: Long)
    // [FIX-SILENCE] El renderer solo produce audio para objetos activos en
    // su lista interna (setObjects()/objectsA_/objectsB_), que ANTES nunca
    // se poblaba: el motor corría (upmixer + renderer + HRTF) pero
    // numActiveObjects_ quedaba en 0 para siempre -> salida binaural
    // silenciosa. Este puente sincroniza las 4 posiciones de stem del
    // upmixer (defaults o custom vía setStemPosition) hacia la lista de
    // objetos activos del renderer.
    external fun nativeObjectRendererSyncStemObjects(rendererHandle: Long, upmixerHandle: Long)

    // NeuralUpmixer
    external fun nativeUpmixerCreate(modelPath: String, sampleRate: Float, blockSize: Int): Long
    external fun nativeUpmixerDestroy(handle: Long)
    external fun nativeUpmixerProcess(handle: Long, inBuffer: java.nio.FloatBuffer, outBuffer: java.nio.FloatBuffer, numFrames: Int)
    external fun nativeUpmixerSetEnabled(handle: Long, enabled: Boolean)
    external fun nativeUpmixerSetStemPosition(handle: Long, stemType: Int, x: Float, y: Float, z: Float, width: Float)
    external fun nativeUpmixerReset(handle: Long)
}
