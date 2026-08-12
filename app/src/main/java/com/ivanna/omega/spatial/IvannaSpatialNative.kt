package com.ivanna.omega.spatial

import com.ivanna.omega.core.NativeLibraryLoader

/**
 * IvannaSpatialNative — Declaración JNI del motor espacial majestuoso.
 *
 * [MAJESTY-KOTLIN-1.0] Esta clase expone al mundo Java/Kotlin el poder de:
 *   - Head Tracking 6DoF (orientación de cabeza en tiempo real)
 *   - Object-Based Renderer (32 objetos 3D simultáneos)
 *   - Neural Upmixer (separación AI de stems + spatialización)
 *
 * Todo el procesamiento ocurre en C++ con zero-allocation y lock-free.
 *
 * AUDIT FIX (JNI signature binding): sin @JvmStatic un `external fun` dentro
 * de un `object` Kotlin se compila como método de INSTANCIA en el bytecode
 * (recibe `jobject thiz` en el JNI). Todo ivanna_spatial_jni.cpp exporta las
 * funciones con la firma estática (JNIEnv*, jclass, ...) — el mismatch causa
 * UnsatisfiedLinkError en runtime al invocar cualquier método spatial. Se
 * anota cada external con @JvmStatic para generar el thunk estático que
 * enlaza con la firma JNI ya existente, sin cambiar APIs externas ni el .cpp.
 */
object IvannaSpatialNative {
    val isLoaded: Boolean = NativeLibraryLoader.ensureLoaded()

    // HeadTracker
    @JvmStatic external fun nativeHeadTrackerCreate(): Long
    @JvmStatic external fun nativeHeadTrackerDestroy(handle: Long)
    @JvmStatic external fun nativeHeadTrackerUpdate(handle: Long, x: Float, y: Float, z: Float, w: Float, timestampMs: Float)
    @JvmStatic external fun nativeHeadTrackerReset(handle: Long)

    // ObjectRenderer
    @JvmStatic external fun nativeObjectRendererCreate(sampleRate: Float, blockSize: Int): Long
    @JvmStatic external fun nativeObjectRendererDestroy(handle: Long)
    @JvmStatic external fun nativeObjectRendererSetHeadTracker(rendererHandle: Long, trackerHandle: Long)
    @JvmStatic external fun nativeObjectRendererSetReverb(handle: Long, level: Float)
    @JvmStatic external fun nativeObjectRendererRenderBlock(
        handle: Long, objectsBuffer: java.nio.FloatBuffer, numObjects: Int,
        outLeftBuffer: java.nio.FloatBuffer, outRightBuffer: java.nio.FloatBuffer, numFrames: Int
    )
    @JvmStatic external fun nativeObjectRendererReset(handle: Long)
    // [FIX-SILENCE] El renderer solo produce audio para objetos activos en
    // su lista interna (setObjects()/objectsA_/objectsB_), que ANTES nunca
    // se poblaba: el motor corría (upmixer + renderer + HRTF) pero
    // numActiveObjects_ quedaba en 0 para siempre -> salida binaural
    // silenciosa. Este puente sincroniza las 4 posiciones de stem del
    // upmixer (defaults o custom vía setStemPosition) hacia la lista de
    // objetos activos del renderer.
    @JvmStatic external fun nativeObjectRendererSyncStemObjects(rendererHandle: Long, upmixerHandle: Long)
    @JvmStatic external fun nativeObjectRendererSetHrtfSubject(handle: Long, subject: String)

    // NeuralUpmixer
    @JvmStatic external fun nativeUpmixerCreate(modelPath: String, sampleRate: Float, blockSize: Int): Long
    @JvmStatic external fun nativeUpmixerDestroy(handle: Long)
    @JvmStatic external fun nativeUpmixerProcess(handle: Long, inBuffer: java.nio.FloatBuffer, outBuffer: java.nio.FloatBuffer, numFrames: Int)
    @JvmStatic external fun nativeUpmixerSetEnabled(handle: Long, enabled: Boolean)
    @JvmStatic external fun nativeUpmixerSetStemPosition(handle: Long, stemType: Int, x: Float, y: Float, z: Float, width: Float)
    @JvmStatic external fun nativeUpmixerReset(handle: Long)
}
