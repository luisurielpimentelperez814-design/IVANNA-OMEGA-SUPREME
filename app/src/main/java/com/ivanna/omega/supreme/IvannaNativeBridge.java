package com.ivanna.omega.supreme;

import java.nio.ByteBuffer;

/**
 * IvannaNativeBridge — puente JVM ↔ IvannaFusionEngine (C++).
 *
 * FIX (memory leak): la clase no implementaba AutoCloseable ni tenía
 * finalize() — si el caller se olvidaba de llamar close(), el engine
 * nativo (IvannaFusionEngine*, heap C++) nunca se liberaba. En el
 * proceso de Android, esto se acumula silenciosamente: cada instancia
 * huérfana retiene el heap C++ completo (HrtfManager, EvolutionaryEQ,
 * Psychoacoustics, IvannaAudioClassifier + sus buffers internos).
 *
 * FIX (race condition): processAudioBlock y processDirectBuffer no
 * estaban sincronizados — dos hilos (el de audio y el de telemetría)
 * podían llamarlos concurrentemente sobre el mismo engine nativo,
 * produciendo datos corruptos o SIGSEGV dentro de IvannaFusionEngine.
 * Ahora se sincronizan con el mismo lock que close(), garantizando
 * que no quede ninguna llamada nativa en vuelo cuando close() libera
 * el puntero.
 *
 * finalize() como red de seguridad (last resort): obsoleto en Android 9+
 * pero funcional hasta que el GC decide colectar. Sigue siendo la única
 * barrera extra que evita leaks en código legacy que no usa try-with-resources.
 */
public class IvannaNativeBridge implements AutoCloseable {
    static {
        System.loadLibrary("ivanna_omega_native");
    }

    private volatile long nativeHandle = 0;

    public IvannaNativeBridge() {
        nativeHandle = nativeInitEngine();
    }

    @Override
    public synchronized void close() {
        if (nativeHandle != 0) {
            nativeDestroyEngine(nativeHandle);
            nativeHandle = 0;
        }
    }

    public synchronized void processAudioBlock(float[] left, float[] right, int numSamples) {
        if (nativeHandle != 0) {
            nativeProcessAudioBlock(nativeHandle, left, right, numSamples);
        }
    }

    public synchronized void processDirectBuffer(ByteBuffer directBuffer, int numFrames) {
        if (nativeHandle != 0 && directBuffer.isDirect()) {
            nativeProcessDirectBuffer(nativeHandle, directBuffer, numFrames);
        }
    }

    public float[] getClassifierProbabilities() {
        float[] probs = new float[4];
        final long h;
        synchronized (this) { h = nativeHandle; }
        if (h != 0) {
            nativeGetClassifierProbabilities(h, probs);
        }
        return probs;
    }

    public int getDominantClass() {
        final long h;
        synchronized (this) { h = nativeHandle; }
        return (h != 0) ? nativeGetDominantClass(h) : 0;
    }

    public synchronized void setGoldenEarMode(boolean enable) {
        if (nativeHandle != 0) {
            nativeSetGoldenEarMode(nativeHandle, enable);
        }
    }

    public synchronized void runAcousticProfiling() {
        if (nativeHandle != 0) {
            nativeRunAcousticProfiling(nativeHandle);
        }
    }

    /**
     * Red de seguridad GC: libera el engine nativo si el caller
     * olvidó llamar close(). Suprimido porque finalize() está
     * deprecated en API 29+ — la forma correcta es try-with-resources.
     */
    @SuppressWarnings("deprecation")
    @Override
    protected void finalize() throws Throwable {
        try {
            close();
        } finally {
            super.finalize();
        }
    }

    // Native C++ Declarations
    private static native long nativeInitEngine();
    private static native void nativeDestroyEngine(long handle);
    private static native void nativeProcessAudioBlock(long handle, float[] left, float[] right, int numSamples);
    private static native void nativeProcessDirectBuffer(long handle, ByteBuffer directBuffer, int numFrames);
    private static native void nativeGetClassifierProbabilities(long handle, float[] outProbs);
    private static native int nativeGetDominantClass(long handle);
    private static native void nativeSetGoldenEarMode(long handle, boolean enable);
    private static native void nativeRunAcousticProfiling(long handle);
}
