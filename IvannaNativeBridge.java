package com.ivanna.omega.supreme;

import java.nio.ByteBuffer;

public class IvannaNativeBridge {
    static {
        System.loadLibrary("ivanna_omega_native");
    }

    private long nativeHandle = 0;

    public IvannaNativeBridge() {
        nativeHandle = nativeInitEngine();
    }

    public synchronized void close() {
        if (nativeHandle != 0) {
            nativeDestroyEngine(nativeHandle);
            nativeHandle = 0;
        }
    }

    public void processAudioBlock(float[] left, float[] right, int numSamples) {
        if (nativeHandle != 0) {
            nativeProcessAudioBlock(nativeHandle, left, right, numSamples);
        }
    }

    public void processDirectBuffer(ByteBuffer directBuffer, int numFrames) {
        if (nativeHandle != 0 && directBuffer.isDirect()) {
            nativeProcessDirectBuffer(nativeHandle, directBuffer, numFrames);
        }
    }

    public float[] getClassifierProbabilities() {
        float[] probs = new float[4];
        if (nativeHandle != 0) {
            nativeGetClassifierProbabilities(nativeHandle, probs);
        }
        return probs;
    }

    public int getDominantClass() {
        return (nativeHandle != 0) ? nativeGetDominantClass(nativeHandle) : 0;
    }

    public void setGoldenEarMode(boolean enable) {
        if (nativeHandle != 0) {
            nativeSetGoldenEarMode(nativeHandle, enable);
        }
    }

    public void runAcousticProfiling() {
        if (nativeHandle != 0) {
            nativeRunAcousticProfiling(nativeHandle);
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
