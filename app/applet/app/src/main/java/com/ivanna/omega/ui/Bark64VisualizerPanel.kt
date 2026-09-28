package com.ivanna.omega.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.unit.dp
import com.ivanna.omega.ui.theme.AuroraCyan
import com.ivanna.omega.visualizer.IvannaVisualizerBark64Bridge

/**
 * Bark64VisualizerPanel — espectro real de 64 bandas perceptuales Bark.
 *
 * FIX (auditoría 2026-08-12): IvannaVisualizerBark64Bridge.sampleInto() y
 * .isReady tenían CERO callers en toda la UI real (grep confirmado sobre
 * app/src/main/java/com/ivanna/omega/ui). El pipeline nativo -- JNI
 * (ivanna_visualizer_bark64_jni.cpp) <-> bridge Kotlin <-> alimentacion
 * real de audio capturado en PlaybackCaptureService.kt -- estaba
 * correctamente cableado de punta a punta, pero nadie leia la salida. El
 * unico archivo que toco "AudioVisualizer" en el commit que introdujo esto
 * fue src/components/AudioVisualizer.tsx, un mockup React/Vite en la raiz
 * del repo (package.json: "name": "react-example") que NO se compila ni
 * se shippea con la app Android -- completamente desconectado del APK real.
 *
 * Este panel cierra el hueco: pollea sampleInto() cada frame via
 * withFrameNanos (runtime de Compose, sincronizado con el refresh real de
 * pantalla -- no un delay fijo arbitrario) y dibuja las 64 bandas como
 * barras verticales. Sin suavizado adicional en Kotlin: sampleForRender()
 * ya hace ballistics/decay del lado nativo; suavizar otra vez aqui solo
 * anadiria latencia visual sin beneficio.
 */
@Composable
fun Bark64VisualizerPanel(modifier: Modifier = Modifier) {
    val bandCount = IvannaVisualizerBark64Bridge.BAND_COUNT
    var bands by remember { mutableStateOf(FloatArray(bandCount)) }

    LaunchedEffect(Unit) {
        while (true) {
            withFrameNanos { }
            if (IvannaVisualizerBark64Bridge.isReady) {
                val next = FloatArray(bandCount)
                IvannaVisualizerBark64Bridge.sampleInto(next)
                bands = next
            }
        }
    }

    Canvas(modifier = modifier.fillMaxWidth().height(80.dp)) {
        if (bandCount == 0) return@Canvas
        val barWidth = size.width / bandCount
        for (i in 0 until bandCount) {
            val v = bands.getOrElse(i) { 0f }.coerceIn(0f, 1f)
            val barHeight = v * size.height
            drawRect(
                brush = Brush.verticalGradient(
                    colors = listOf(AuroraCyan, AuroraCyan.copy(alpha = 0.35f)),
                    startY = size.height - barHeight,
                    endY = size.height
                ),
                topLeft = Offset(i * barWidth + barWidth * 0.1f, size.height - barHeight),
                size = Size(barWidth * 0.8f, barHeight.coerceAtLeast(0f))
            )
        }
    }
}
