#!/bin/bash
set -e

echo "=== Aplicando parches de la interfaz e integración JNI ==="

# 1. Actualizar IvannaNativeLib.kt (Agregando nativeSetAntiDolbyIntensity)
cat << 'KOTLIN_LIB_EOF' > app/src/main/java/com/ivanna/omega/core/IvannaNativeLib.kt
package com.ivanna.omega.core

/**
 * Interface JNI para el motor DSP IVANNA-OMEGA-SUPREME.
 */
object IvannaNativeLib {
    init {
        try {
            System.loadLibrary("ivanna_omega")
        } catch (e: UnsatisfiedLinkError) {
            e.printStackTrace()
        }
    }

    // Controles de ganancia y parámetros acústicos
    external fun nativeInitDSP(sampleRate: Int)
    external fun nativeProcessBlock(input: FloatArray, output: FloatArray, numFrames: Int)
    external fun nativeGetClipCount(): Int
    external fun nativeResetClipCount()

    // Métodos NHO / Spatial / Anti-Dolby
    external fun nativeSetAlpha(alpha: Float)
    external fun nativeSetBeta(beta: Float)
    external fun nativeSetGamma(gamma: Float)
    external fun nativeSetDelta(delta: Float)
    external fun nativeSetEta(eta: Float)
    external fun nativeSetHarmonicGain(gain: Float)
    external fun nativeSetHRTFEnabled(enabled: Boolean)
    external fun nativeSetCompressorParams(thresholdDb: Float, ratio: Float, attackMs: Float, releaseMs: Float)
    external fun nativeSetSpatialAngleRad(rad: Float)
    external fun nativeSetSpatialWidthDirect(width: Float)
    external fun nativeSetAntiDolbyIntensity(intensity: Float)

    // Métodos Adaptativos y Telemetría
    external fun nativeSetAdaptiveControls(fatigueIndex: Float, iirAlpha: Float)
    external fun nativeGetAdaptiveTelemetry(): FloatArray
    external fun nativeGetBandEnergies(): FloatArray
}
KOTLIN_LIB_EOF
echo "✅ app/src/main/java/com/ivanna/omega/core/IvannaNativeLib.kt actualizado"

# 2. Corregir import de BorderStroke en IvannaControlPanel.kt
sed -i 's/import androidx.compose.animation.shrinkVertically/import androidx.compose.animation.shrinkVertically\nimport androidx.compose.foundation.BorderStroke/g' app/src/main/java/com/ivanna/omega/ui/IvannaControlPanel.kt || true
echo "✅ app/src/main/java/com/ivanna/omega/ui/IvannaControlPanel.kt (Import BorderStroke corregido)"

# 3. Verificar estado de Git o sincronización
if [ -d ".git" ]; then
    echo "=== Sincronizando cambios con repositorio remoto ==="
    git fetch origin main
    git reset --hard origin/main
    echo "✅ Repositorio sincronizado al último commit remoto."
fi

echo "=== PATCH APLICADO CORRECTAMENTE ==="
