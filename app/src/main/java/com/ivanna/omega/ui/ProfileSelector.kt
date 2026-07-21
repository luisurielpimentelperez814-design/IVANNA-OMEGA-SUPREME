package com.ivanna.omega.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.audio.IvannaAudioProfile
import com.ivanna.omega.audio.IvannaProfileMetadata
import com.ivanna.omega.ui.theme.*

/**
 * ProfileSelector — pantalla de selección y aplicación de presets de audio.
 *
 * REGLAS DE CONEXIÓN (v3.0):
 *   - No depende de ProfileManager legacy (roto, depende de AudioEngine sin
 *     setters públicos de gain/exciter/eq). Carga perfiles via ProfilesLoader
 *     (res/raw/audio_profiles.json).
 *   - onApply callback recibe el AudioProfile y MainActivity decide cómo
 *     aplicarlo al DSP real (dspState / DSPBridge / globalEffectManager /
 *     NativeLib / OmegaEngineBridge — todas las rutas ya cableadas en
 *     sesiones previas, ver commit 4e77fac).
 *   - onClose devuelve control al MainActivity (cierra overlay).
 */
@Composable
fun ProfileSelectorScreen(
    profiles: List<IvannaAudioProfile>,
    metadata: IvannaProfileMetadata?,
    currentId: String?,
    onApply: (IvannaAudioProfile) -> Unit,
    onClose: () -> Unit,
    modifier: Modifier = Modifier
) {
    var activeId by remember { mutableStateOf(currentId) }
    var showTraining by remember { mutableStateOf(false) }

    Column(
        modifier = modifier
            .fillMaxSize()
            .background(ObsidianVoid)
            .padding(16.dp)
    ) {
        // ── Header ─────────────────────────────────────────────
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    "IVANNA AUDIO PROFILES",
                    color = AuroraCyan,
                    fontSize = 18.sp,
                    fontWeight = FontWeight.Bold
                )
                Text(
                    "Presets magistrales · DSP nativo + Magisk system-wide",
                    color = TextSecondary,
                    fontSize = 11.sp
                )
            }
            CloseChip(onClose)
        }

        Spacer(Modifier.height(10.dp))

        // ── Toggle training card ───────────────────────────────
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(8.dp))
                .background(ObsidianSoft.copy(alpha = 0.6f))
                .border(1.dp, ObsidianEdge, RoundedCornerShape(8.dp))
                .clickable { showTraining = !showTraining }
                .padding(10.dp)
        ) {
            Text(
                if (showTraining) "▼ Ocultar secuencia de entrenamiento"
                else "▶ Mostrar secuencia recomendada",
                color = AuroraCyan,
                fontSize = 11.sp,
                fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
            )
        }

        if (showTraining && metadata != null) {
            Spacer(Modifier.height(8.dp))
            TrainingCard(metadata = metadata, profiles = profiles)
            Spacer(Modifier.height(8.dp))
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(6.dp))
                    .background(ObsidianSoft.copy(alpha = 0.7f))
                    .padding(8.dp)
            ) {
                Text(
                    metadata.trainingNotes,
                    color = TextSecondary,
                    fontSize = 10.sp,
                    fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
                )
            }
        }

        Spacer(Modifier.height(12.dp))

        // ── Lista de perfiles ─────────────────────────────────
        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            items(profiles) { profile ->
                ProfileCard(
                    profile = profile,
                    isSelected = activeId == profile.id,
                    onApply = {
                        activeId = profile.id
                        onApply(profile)
                    }
                )
            }
        }
    }
}

@Composable
private fun ProfileCard(
    profile: IvannaAudioProfile,
    isSelected: Boolean,
    onApply: () -> Unit,
    modifier: Modifier = Modifier
) {
    val bgColor     = if (isSelected) AuroraCyan.copy(alpha = 0.12f) else ObsidianSoft.copy(alpha = 0.55f)
    val borderColor = if (isSelected) AuroraCyan else ObsidianEdge
    val accentColor = if (isSelected) AuroraCyan else TextMuted

    Box(
        modifier = modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(10.dp))
            .background(bgColor)
            .border(1.5.dp, borderColor, RoundedCornerShape(10.dp))
            .clickable(onClick = onApply)
            .padding(12.dp)
    ) {
        Column {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    profile.name,
                    color = TextPrimary,
                    fontSize = 16.sp,
                    fontWeight = FontWeight.Bold,
                    modifier = Modifier.weight(1f)
                )
                if (isSelected) {
                    Text(
                        "● ACTIVO",
                        color = AuroraCyan,
                        fontSize = 10.sp,
                        fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace,
                        fontWeight = FontWeight.Bold
                    )
                } else {
                    Text(
                        categoryLabel(profile.category),
                        color = accentColor,
                        fontSize = 9.sp,
                        fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
                    )
                }
            }

            Spacer(Modifier.height(4.dp))
            Text(
                profile.description,
                color = TextSecondary,
                fontSize = 11.sp
            )

            Spacer(Modifier.height(8.dp))
            ParametersRow(profile)

            if (profile.tags.isNotEmpty()) {
                Spacer(Modifier.height(8.dp))
                TagsRow(tags = profile.tags)
            }
        }
    }
}

@Composable
private fun ParametersRow(profile: IvannaAudioProfile) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        ParamCell("GAIN",     "%.2f".format(profile.audioEngine.gain),         TextSecondary)
        ParamCell("EXCITER",  "%.2f".format(profile.audioEngine.exciterAmount), AmberSignal)
        ParamCell("EQ",       "%+.1f dB".format(profile.audioEngine.eqGain),  AuroraCyan)
        ParamCell("WIDTH",    "%.2f".format(profile.audioEngine.widthAmount),  NeonMagenta)
    }
}

@Composable
private fun ParamCell(label: String, value: String, accent: Color) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Text(
            label,
            color = TextMuted,
            fontSize = 8.sp,
            fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
        )
        Text(
            value,
            color = accent,
            fontSize = 12.sp,
            fontWeight = FontWeight.Bold,
            fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
        )
    }
}

@Composable
private fun TagsRow(tags: List<String>) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .horizontalScroll(rememberScrollState()),
        horizontalArrangement = Arrangement.spacedBy(4.dp)
    ) {
        tags.forEach { tag ->
            Box(
                modifier = Modifier
                    .clip(RoundedCornerShape(10.dp))
                    .background(AuroraCyan.copy(alpha = 0.18f))
                    .padding(horizontal = 8.dp, vertical = 3.dp)
            ) {
                Text(
                    tag,
                    color = AuroraCyan,
                    fontSize = 9.sp,
                    fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
                )
            }
        }
    }
}

@Composable
private fun TrainingCard(
    metadata: IvannaProfileMetadata,
    profiles: List<IvannaAudioProfile>
) {
    val ordered = metadata.trainingSequence.mapNotNull { id -> profiles.find { it.id == id } }
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(8.dp))
            .background(AmberSignal.copy(alpha = 0.08f))
            .border(1.dp, AmberSignal.copy(alpha = 0.4f), RoundedCornerShape(8.dp))
            .padding(10.dp)
    ) {
        ordered.forEachIndexed { idx, profile ->
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    "DÍA ${idx + 1}",
                    color = AmberSignal,
                    fontSize = 10.sp,
                    fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace,
                    fontWeight = FontWeight.Bold,
                    modifier = Modifier.width(56.dp)
                )
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        profile.name,
                        color = TextPrimary,
                        fontSize = 12.sp,
                        fontWeight = FontWeight.SemiBold
                    )
                    Text(
                        profile.description,
                        color = TextSecondary,
                        fontSize = 9.sp,
                        fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
                    )
                }
            }
            if (idx < ordered.lastIndex) {
                Spacer(Modifier.height(6.dp))
            }
        }
    }
}

@Composable
private fun CloseChip(onClose: () -> Unit) {
    Box(
        modifier = Modifier
            .clip(RoundedCornerShape(8.dp))
            .background(NeonMagenta.copy(alpha = 0.18f))
            .border(1.dp, NeonMagenta, RoundedCornerShape(8.dp))
            .clickable(onClick = onClose)
            .padding(horizontal = 12.dp, vertical = 6.dp)
    ) {
        Text(
            "✕ CERRAR",
            color = NeonMagenta,
            fontSize = 11.sp,
            fontWeight = FontWeight.Bold,
            fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
        )
    }
}

private fun categoryLabel(category: String): String = when (category) {
    "classic_rock"    -> "CLASSIC ROCK"
    "progressive_rock"-> "PROGRESSIVE"
    "hard_rock"       -> "HARD ROCK"
    else              -> category.uppercase()
}

