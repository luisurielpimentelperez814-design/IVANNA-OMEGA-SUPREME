package com.ivanna.omega.spatial

import android.content.Context
import android.graphics.Bitmap
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
