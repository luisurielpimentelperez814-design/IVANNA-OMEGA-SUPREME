package com.ivanna.omega.spatial

import android.content.Context
import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.launch
import com.ivanna.omega.core.IvannaNativeLib
import java.io.BufferedInputStream
import java.io.File
import java.io.FileInputStream
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * Adapter for the native, production-geometry PPM renderer.
 * The saved artifact remains full 4K; preview bitmap is downsampled to cap memory use.
 */
object RoomGeometryPreview {
    data class Result(val file: File, val bitmap: Bitmap, val sourceWidth: Int, val sourceHeight: Int)

    suspend fun render(context: Context, width: Int = 3840, height: Int = 2160): Result? =
        withContext(Dispatchers.IO) {
            if (!IvannaNativeLib.isLoaded || width !in 1..3840 || height !in 1..2160) return@withContext null
            val dir = context.filesDir
            if (!IvannaNativeLib.nativeRenderRoomGeometry(dir.absolutePath, width, height)) return@withContext null
            val file = File(dir, "ivanna_room_geometry.ppm")
            if (!file.isFile || file.length() < 16L) return@withContext null
            BufferedInputStream(FileInputStream(file), 64 * 1024).use { input ->
                fun token(): String {
                    val b = StringBuilder()
                    var ch: Int
                    do { ch = input.read() } while (ch >= 0 && ch.toChar().isWhitespace())
                    while (ch >= 0 && !ch.toChar().isWhitespace()) {
                        b.append(ch.toChar()); ch = input.read()
                    }
                    return b.toString()
                }
                if (token() != "P6") return@withContext null
                val w = token().toIntOrNull() ?: return@withContext null
                val h = token().toIntOrNull() ?: return@withContext null
                if (token() != "255" || w != width || h != height) return@withContext null
                val factor = maxOf(1, (w + 1919) / 1920, (h + 1079) / 1080)
                val outW = (w + factor - 1) / factor
                val outH = (h + factor - 1) / factor
                val pixels = IntArray(outW * outH)
                for (y in 0 until h) for (x in 0 until w) {
                    val r = input.read(); val g = input.read(); val b = input.read()
                    if (r < 0 || g < 0 || b < 0) return@withContext null
                    if (x % factor == 0 && y % factor == 0) {
                        pixels[(y / factor) * outW + (x / factor)] =
                            (0xff shl 24) or (r shl 16) or (g shl 8) or b
                    }
                }
                val bitmap = Bitmap.createBitmap(outW, outH, Bitmap.Config.ARGB_8888)
                bitmap.setPixels(pixels, 0, outW, 0, 0, outW, outH)
                Result(file, bitmap, w, h)
            }
        }
}


@Composable
fun RoomGeometryPreviewPanel() {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var result by remember { mutableStateOf<RoomGeometryPreview.Result?>(null) }
    var busy by remember { mutableStateOf(false) }
    Column {
        OutlinedButton(enabled = !busy, onClick = {
            busy = true
            scope.launch {
                result = RoomGeometryPreview.render(context)
                busy = false
            }
        }) { Text(if (busy) "RENDERIZANDO 3D 4K…" else "GENERAR VISTA GEOMÉTRICA 3D 4K") }
        result?.let { frame ->
            Image(bitmap = frame.bitmap.asImageBitmap(), contentDescription = "Geometría tridimensional de la sala IVANNA",
                modifier = Modifier.fillMaxWidth().heightIn(max = 240.dp))
            Text("Render ${frame.sourceWidth}×${frame.sourceHeight} · vista previa reducida para limitar memoria · PPM completo en almacenamiento privado")
        }
    }
}
