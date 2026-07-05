package com.ivanna.omega.visualizer

import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.viewinterop.AndroidView

/**
 * [FIX-AURORA-BG-2] antes usaba GLSurfaceView (SurfaceView), que solo puede
 * componerse por delante o por detrás de TODA la ventana, nunca "entre"
 * Views normales — obligaba a hacer la ventana translúcida para que se
 * viera detrás del panel, lo que congelaba la app entera (ver GLTextureView.kt).
 * GLTextureView es una View normal: se compone en el orden en que aparece
 * en el árbol, sin necesidad de ventana translúcida ni zOrder especial.
 */
@Composable
fun VisualizerSurface(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    AndroidView(
        modifier = modifier.fillMaxSize(),
        factory = { ctx ->
            GLTextureView(ctx).apply {
                setRenderer(VisualizerRendererV2(context))
            }
        }
    )
}
