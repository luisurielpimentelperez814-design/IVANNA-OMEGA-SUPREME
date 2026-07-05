package com.ivanna.omega.visualizer

import android.opengl.GLSurfaceView
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.viewinterop.AndroidView

@Composable
fun VisualizerSurface(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    AndroidView(
        modifier = modifier.fillMaxSize(),
        factory = { ctx ->
            GLSurfaceView(ctx).apply {
                setEGLContextClientVersion(3)
                // [FIX-AURORA-BG] zOrderOnTop(true) ponía este SurfaceView por
                // encima de TODA la ventana (incluido el panel de control) — era
                // necesario cuando este composable ocupaba la pantalla completa
                // en solitario, pero ahora vive detrás del panel como fondo real,
                // así que debe componerse por debajo de la ventana (default).
                // La transparencia de la ventana/Surface de Compose por encima
                // se maneja en MainActivity.onCreate() y en el Surface() raíz.
                setZOrderOnTop(false)
                setRenderer(VisualizerRendererV2(context))
                renderMode = GLSurfaceView.RENDERMODE_CONTINUOUSLY
            }
        }
    )
}
