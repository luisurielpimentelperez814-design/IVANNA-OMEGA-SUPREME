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

@Composable
fun AuditoryExperienceScreen(
    modifier: Modifier = Modifier,
    // DEFAULT PATH: Cambia esto si tus videos están en otra ruta de la memoria interna
    videoFolderPath: String = "/storage/emulated/0/IVANNA/Videos", 
    onEnterMotorClick: () -> Unit = {},
    onVideoSelected: (File) -> Unit = {}
) {
    val context = LocalContext.current
    var videoFiles by remember { mutableStateOf<List<File>>(emptyList()) }
    var selectedVideo by remember { mutableStateOf<File?>(null) }

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
                        }
                    )
                }
            }
        }

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
