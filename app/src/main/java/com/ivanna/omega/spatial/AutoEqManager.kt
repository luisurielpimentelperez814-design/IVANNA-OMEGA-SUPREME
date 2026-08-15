package com.ivanna.omega.spatial

import android.util.Log

object AutoEqManager {
    private const val TAG = "IVANNA.AutoEq"
    
    // A mock database of headphone calibrations
    val availableProfiles = listOf(
        "Sennheiser HD600",
        "Sony WH-1000XM4",
        "Apple AirPods Pro 2",
        "Moondrop Aria",
        "Beyerdynamic DT990 Pro"
    )
    
    fun applyProfile(handle: Long, profileName: String) {
        if (handle == 0L) return
        
        Log.i(TAG, "Applying AutoEQ profile for: \$profileName")
        
        runCatching { IvannaSpatialNative.nativeObjectRendererSetAutoEqEnabled(handle, true) }
        
        // FIX (auditoría 2026-08-15): dentro del mismo when(), algunas llamadas
        // a nativeObjectRendererSetAutoEqBand estaban en runCatching y otras no
        // — mismo método JNI, mismo handle, mismo riesgo, protección parcial e
        // inconsistente. Envolver el bloque completo en un solo runCatching en
        // vez de decorar cada línea suelta: si el handle se invalida a mitad de
        // secuencia, ninguna llamada posterior queda expuesta.
        runCatching {
            // In a real implementation, this would parse a GraphicEQ or ParametricEQ txt from AutoEq
            // Here we mock a generic 5-band compensation for demonstration
            when (profileName) {
                "Sennheiser HD600" -> {
                    IvannaSpatialNative.nativeObjectRendererSetAutoEqBand(handle, 0, 40f, 4.5f, 0.7f)   // Sub-bass boost
                    IvannaSpatialNative.nativeObjectRendererSetAutoEqBand(handle, 1, 3000f, -2.0f, 1.4f) // Upper mid tame
                    IvannaSpatialNative.nativeObjectRendererSetAutoEqBand(handle, 2, 5000f, 1.5f, 2.0f)
                }
                "Sony WH-1000XM4" -> {
                    IvannaSpatialNative.nativeObjectRendererSetAutoEqBand(handle, 0, 150f, -4.0f, 0.7f) // Mid-bass tame
                    IvannaSpatialNative.nativeObjectRendererSetAutoEqBand(handle, 1, 4000f, 2.5f, 1.4f)
                }
                else -> {
                    IvannaSpatialNative.nativeObjectRendererSetAutoEqBand(handle, 0, 100f, 1.0f, 1.0f)
                }
            }
        }
    }
    
    fun disable(handle: Long) {
        if (handle == 0L) return
        runCatching { IvannaSpatialNative.nativeObjectRendererSetAutoEqEnabled(handle, false) }
        Log.i(TAG, "AutoEQ disabled")
    }
}
