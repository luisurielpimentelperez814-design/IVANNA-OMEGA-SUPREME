package com.ivanna.omega.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import coil.compose.AsyncImage
import coil.decode.VideoFrameDecoder
import coil.request.ImageRequest
import coil.request.videoFrameMillis
import java.io.File

// Paleta realineada con IvannaTheme Aurora Obsidiana v3.0 (nombres intactos):
// verde-cian plano → profundidad obsidiana azul del tema central.
val BackgroundDark = Color(0xFF010204)   // ObsidianVoid
val CyanAccent = Color(0xFF6FF3FF)       // AuroraCyan
val CardBackground = Color(0xFF0A101C)   // ObsidianSoft
val CardBorder = Color(0xFF223050)       // ObsidianEdge
val ButtonBackground = Color(0xFF0D1524) // ObsidianGlass
val TextGray = Color(0xFF93A8C6)         // TextSecondary

/** Abre un vídeo con el reproductor del sistema. El motor DSP ya procesa el audio
 *  del sistema (captura activa), así que el vídeo suena a través de IVANNA. */
private fun openVideoInSystemPlayer(context: android.content.Context, uri: android.net.Uri): Boolean =
    runCatching {
        val i = android.content.Intent(android.content.Intent.ACTION_VIEW)
            .setDataAndType(uri, "video/*")
            .addFlags(android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION or
                      android.content.Intent.FLAG_ACTIVITY_NEW_TASK)
        context.startActivity(i); true
    }.getOrDefault(false)

/** content:// del vídeo vía MediaStore (File:// no se puede compartir sin FileProvider). */
private fun mediaStoreUriFor(context: android.content.Context, file: File): android.net.Uri? =
    runCatching {
        context.contentResolver.query(
            android.provider.MediaStore.Video.Media.EXTERNAL_CONTENT_URI,
            arrayOf(android.provider.MediaStore.Video.Media._ID),
            "${android.provider.MediaStore.Video.Media.DATA}=?", arrayOf(file.absolutePath), null
        )?.use { c ->
            if (c.moveToFirst())
                android.content.ContentUris.withAppendedId(
                    android.provider.MediaStore.Video.Media.EXTERNAL_CONTENT_URI, c.getLong(0))
            else null
        }
    }.getOrNull()

@Composable
fun AuditoryExperienceScreen(
    modifier: Modifier = Modifier,
    // DEFAULT PATH: Cambia esto si tus videos están en otra ruta de la memoria interna
    videoFolderPath: String = "/storage/emulated/0/IVANNA/Videos", 
    onEnterMotorClick: () -> Unit = {},
    onVideoSelected: (File) -> Unit = { f ->
        // FIX (auditoría UI 2026-09-20): este callback quedaba con el default
        // vacío porque su único call-site (MainActivity.kt:577) no lo pasa —
        // seleccionar un vídeo no hacía NADA y el gesto se perdía sin rastro.
        // Ahora, como mínimo, la acción se persiste vía UiActionBridge (misma
        // vía que el resto del barrido) para que el call-site futuro pueda
        // recuperar la selección al reiniciar en vez de perderla en silencio.
        com.ivanna.omega.core.UiActionBridge.persist("AuditoryExperienceScreen", "video_selected", f.absolutePath)
    }
) {
    val context = LocalContext.current
    var videoFiles by remember { mutableStateOf<List<File>>(emptyList()) }
    var selectedVideo by remember { mutableStateOf<File?>(null) }
    var statusMsg by remember { mutableStateOf<String?>(null) }
    val picker = androidx.activity.compose.rememberLauncherForActivityResult(
        androidx.activity.result.contract.ActivityResultContracts.OpenDocument()
    ) { uri ->
        if (uri != null) {
            statusMsg = if (openVideoInSystemPlayer(context, uri)) "Reproduciendo — el audio pasa por el motor IVANNA"
                        else "No hay reproductor de vídeo disponible"
        }
    }

    // Escaneo asíncrono para no bloquear la UI
    LaunchedEffect(videoFolderPath) {
        val folder = File(videoFolderPath)
        if (folder.exists() && folder.isDirectory) {
            videoFiles = folder.listFiles { file ->
                file.extension.lowercase() in listOf("mp4", "mkv", "webm", "avi")
            }?.toList() ?: emptyList()
        }
    }

    Column(
        modifier = modifier.fillMaxSize().background(BackgroundDark).padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Spacer(modifier = Modifier.height(24.dp))
        Text("EXPERIENCIA AUDITIVA", color = Color.White, fontSize = 20.sp, fontWeight = FontWeight.ExtraBold, letterSpacing = 2.sp)
        Spacer(modifier = Modifier.height(8.dp))
        Text("Hard Rock 70s • DSP + HRTF + Neuromorphic", color = TextGray, fontSize = 11.sp)
        Spacer(modifier = Modifier.height(32.dp))

        if (videoFiles.isEmpty()) {
            Box(modifier = Modifier.weight(1f), contentAlignment = Alignment.Center) {
                Text("No se encontraron videos en:\n$videoFolderPath", color = TextGray, textAlign = TextAlign.Center)
            }
        } else {
            LazyVerticalGrid(
                columns = GridCells.Fixed(3),
                horizontalArrangement = Arrangement.spacedBy(10.dp),
                verticalArrangement = Arrangement.spacedBy(10.dp),
                modifier = Modifier.weight(1f)
            ) {
                items(videoFiles) { file ->
                    VideoThumbnailCard(
                        file = file,
                        isSelected = selectedVideo == file,
                        onClick = { 
                            selectedVideo = file
                            onVideoSelected(file)
                            val uri = mediaStoreUriFor(context, file)
                            statusMsg = when {
                                uri == null -> "No pude abrir ese archivo; usa ELEGIR VIDEO"
                                openVideoInSystemPlayer(context, uri) -> "Reproduciendo — el audio pasa por el motor IVANNA"
                                else -> "No hay reproductor de vídeo disponible"
                            }
                        }
                    )
                }
            }
        }

        statusMsg?.let {
            Text(it, color = CyanAccent, fontSize = 11.sp, textAlign = TextAlign.Center)
            Spacer(modifier = Modifier.height(8.dp))
        }
        Button(
            onClick = { picker.launch(arrayOf("video/*")) },
            shape = RoundedCornerShape(12.dp),
            colors = ButtonDefaults.buttonColors(containerColor = ButtonBackground, contentColor = Color.White),
            modifier = Modifier.fillMaxWidth().height(48.dp).border(1.dp, CardBorder, RoundedCornerShape(12.dp))
        ) { Text("ELEGIR VIDEO", fontSize = 13.sp, fontWeight = FontWeight.Bold) }
        Spacer(modifier = Modifier.height(10.dp))
        Button(
            onClick = onEnterMotorClick,
            shape = RoundedCornerShape(12.dp),
            colors = ButtonDefaults.buttonColors(containerColor = ButtonBackground, contentColor = Color.White),
            modifier = Modifier.fillMaxWidth().height(60.dp).border(1.dp, CyanAccent, RoundedCornerShape(12.dp))
        ) {
            Text("ENTRAR AL MOTOR", fontSize = 14.sp, fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
        }
        Spacer(modifier = Modifier.height(16.dp))
    }
}

@Composable
fun VideoThumbnailCard(file: File, isSelected: Boolean, onClick: () -> Unit) {
    val context = LocalContext.current
    val model = remember(file) {
        ImageRequest.Builder(context)
            .data(file)
            .videoFrameMillis(1000) // Extrae el fotograma exactamente en el segundo 1 del video
            .decoderFactory(VideoFrameDecoder.Factory())
            .crossfade(true)
            .build()
    }

    Box(
        modifier = Modifier
            .aspectRatio(1.4f)
            .background(CardBackground, RoundedCornerShape(8.dp))
            .border(1.dp, if (isSelected) CyanAccent else CardBorder, RoundedCornerShape(8.dp))
            .clickable { onClick() }
            .padding(2.dp),
        contentAlignment = Alignment.Center
    ) {
        AsyncImage(
            model = model,
            contentDescription = file.name,
            contentScale = ContentScale.Crop,
            modifier = Modifier.fillMaxSize()
        )
        // Filtro oscuro encima de la miniatura para que el nombre se pueda leer
        Box(modifier = Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.6f)))

        Text(
            text = file.nameWithoutExtension,
            color = Color.White,
            fontSize = 10.sp,
            textAlign = TextAlign.Center,
            lineHeight = 12.sp,
            modifier = Modifier.padding(4.dp)
        )
    }
}
