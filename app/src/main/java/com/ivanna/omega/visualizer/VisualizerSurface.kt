package com.ivanna.omega.visualizer

import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver

/**
 * [FIX-AURORA-BG-2] Usa GLTextureView (no GLSurfaceView) para que se
 * componga como una View normal detrás del panel Compose sin necesidad
 * de ventana translúcida.
 *
 * [FIX-AURORA-BG-4.5] Ata el ciclo de vida:
 *   - onPause de la Activity  → pausa el hilo GL (deja de quemar batería)
 *   - onResume de la Activity → reanuda el render
 *   - onDispose del Composable → apaga el hilo GL de forma limpia
 * Antes el hilo GL vivía atado únicamente al SurfaceTexture, así que se
 * quedaba renderizando incluso con la Activity en background si el
 * SurfaceTexture no era destruido (p. ej. sistema Android manteniéndolo
 * cacheado). Esto congelaba otras apps y calentaba el dispositivo.
 */
@Composable
fun VisualizerSurface(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val lifecycleOwner = LocalLifecycleOwner.current
    var view by remember { mutableStateOf<GLTextureView?>(null) }

    DisposableEffect(lifecycleOwner, view) {
        val v = view
        val observer = LifecycleEventObserver { _, event ->
            when (event) {
                Lifecycle.Event.ON_RESUME -> v?.onResume()
                Lifecycle.Event.ON_PAUSE -> v?.onPause()
                else -> Unit
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose {
            lifecycleOwner.lifecycle.removeObserver(observer)
        }
    }

    AndroidView(
        modifier = modifier.fillMaxSize(),
        factory = { ctx ->
            GLTextureView(ctx).apply {
                setRenderer(VisualizerRendererV2(ctx))
                view = this
            }
        }
    )
}
