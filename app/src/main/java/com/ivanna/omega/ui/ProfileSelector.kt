package com.ivanna.omega.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.border
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.foundation.border
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

    // ── Los siete perfiles Hard Rock 70s del prototipo (Pantalla II) ──────
    // Orden del prototipo y de MainActivity.IntroScreen (Grand Funk, Led
    // Zeppelin, Rush, Budgie, Edgar Winter, Steve Miller, BTO): Rush queda
    // III / VII. El resto de perfiles sigue disponible debajo, sin cambios.
    val sevenProfiles = remember(profiles) {
        profiles.filter { it.id in sevenProfileOrder }
            .sortedBy { sevenProfileOrder.indexOf(it.id) }
    }
    val otherProfiles = remember(profiles) { profiles.filter { it.id !in sevenProfileOrder } }
    // Destacado: el perfil activo si es uno de los siete; si no hay selección
    // (o es un perfil general), Rush, el preseleccionado del prototipo.
    val featuredProfile = sevenProfiles.find { it.id == activeId }
        ?: sevenProfiles.find { it.id == "rush" }

    Column(
        modifier = modifier
            .fillMaxSize()
            .background(MagistralVoid)
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
                    color = MagistralGoldHi,
                    fontSize = 18.sp,
                    fontWeight = FontWeight.Bold,
                    fontFamily = MagistralSerif
                )
                Text(
                    "Presets magistrales · DSP nativo + Magisk system-wide",
                    color = MagistralMuted,
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
                .background(MagistralPanel.copy(alpha = 0.6f))
                .border(1.dp, MagistralLine, RoundedCornerShape(8.dp))
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
                    .background(MagistralPanel.copy(alpha = 0.7f))
                    .padding(8.dp)
            ) {
                Text(
                    metadata.trainingNotes,
                    color = MagistralMuted,
                    fontSize = 10.sp,
                    fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
                )
            }
        }

        Spacer(Modifier.height(12.dp))

        // ── Lista de perfiles ─────────────────────────────────
        LazyColumn(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            // ── Ficha de la pantalla (prototipo, Pantalla II) ─────────────
            item { ScreenSpecCard(spec = ExperienceScreenSpec) }

            // ── Perfil destacado ──────────────────────────────────────────
            featuredProfile?.let { featured ->
                item {
                    FeaturedProfileCard(
                        profile = featured,
                        positionLabel = "${romanNumeral(sevenProfiles.indexOf(featured) + 1)} / VII"
                    )
                }
            }

            // ── Parrilla de los siete perfiles ────────────────────────────
            items(sevenProfiles) { profile ->
                ProfileCard(
                    profile = profile,
                    isSelected = activeId == profile.id,
                    onApply = {
                        activeId = profile.id
                        onApply(profile)
                    }
                )
            }

            // ── El resto de perfiles sigue disponible, sin cambios ────────
            if (otherProfiles.isNotEmpty()) {
                item {
                    Text(
                        "OTROS PERFILES · GENERALES",
                        color = MagistralMuted,
                        fontSize = 10.sp,
                        fontWeight = FontWeight.ExtraBold,
                        letterSpacing = 2.sp,
                        modifier = Modifier.padding(top = 8.dp)
                    )
                }
                items(otherProfiles) { profile ->
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
}

@Composable
private fun ProfileCard(
    profile: IvannaAudioProfile,
    isSelected: Boolean,
    onApply: () -> Unit,
    modifier: Modifier = Modifier
) {
    val bgColor     = if (isSelected) MagistralGold.copy(alpha = 0.12f) else MagistralPanel
    val borderColor = if (isSelected) MagistralGold else MagistralLine
    val accentColor = if (isSelected) AuroraCyan else MagistralMuted2

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
                    color = MagistralText,
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
                        prototypeProfileTags[profile.id] ?: categoryLabel(profile.category),
                        color = accentColor,
                        fontSize = 9.sp,
                        fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
                    )
                }
                Spacer(Modifier.width(8.dp))
                SpecInfoButton(spec = profileSpec(profile))
            }

            Spacer(Modifier.height(4.dp))
            Text(
                profile.description,
                color = MagistralMuted,
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
        ParamCell("GAIN",     "%.2f".format(profile.audioEngine.gain),         MagistralMuted)
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
            color = MagistralMuted2,
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
                        color = MagistralText,
                        fontSize = 12.sp,
                        fontWeight = FontWeight.SemiBold
                    )
                    Text(
                        profile.description,
                        color = MagistralMuted,
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

// ── Refinamiento magistral v2.5.442 — Pantalla II · Experiencia ────────────
// Textos verbatim del prototipo aprobado
// (~/workspace/ivanna-ui-refinement-text.txt + ivanna-ui-refinement.html).
// Sin datos inventados: cada descripción y cada etiqueta salen del prototipo.

/** Ficha de la pantalla Experiencia (prototipo, Pantalla II, verbatim). */
val ExperienceScreenSpec = SpecInfo(
    title = "Experiencia Auditiva",
    what = "El selector de perfiles de escucha: siete firmas medidas de bandas de hard rock 70s. No es un ecualizador: cada perfil es una curva completa de destino para la cadena DSP + HRTF + Neuromorphic.",
    purpose = "Para decirle al motor cómo debe sonar el material antes de tocar un solo control: el perfil fija el carácter (pegada, aire, filo, espacio) y los entornos siguientes lo ejecutan.",
    whenUse = "Elige el perfil de la banda que vas a escuchar, o el más cercano en carácter si es otra música: Rush para precisión y transitorios, Steve Miller para el campo más abierto, Grand Funk para potencia con cuerpo.",
    whenAvoid = "No cambies de perfil a mitad de canción para \"arreglar\" una mezcla: el oído necesita unos segundos para asentarse. Y no uses un perfil de filo (Budgie) con grabaciones ya ásperas: suma aspereza."
)

/** Orden de los siete perfiles en el prototipo (y en MainActivity.IntroScreen). */
private val sevenProfileOrder = listOf(
    "grand_funk", "led_zeppelin", "rush", "budgie",
    "edgar_winter", "steve_miller", "bachman_turner_overdrive"
)

/** Nombre corto de cada perfil, como aparece en el prototipo. */
private fun prototypeShortName(id: String): String? = when (id) {
    "rush" -> "Rush"
    "grand_funk" -> "Grand Funk Railroad"
    "led_zeppelin" -> "Led Zeppelin"
    "budgie" -> "Budgie"
    "edgar_winter" -> "Edgar Winter"
    "steve_miller" -> "Steve Miller Band"
    "bachman_turner_overdrive" -> "Bachman-Turner Overdrive"
    else -> null
}

/** Descripción de cada perfil, verbatim del prototipo (HTML, Pantalla II). */
private val prototypeProfileDescriptions = mapOf(
    "rush" to "Transitorios de tarola con filo, armónicos de bajo presentes y escena progresiva amplia. El perfil de precisión de la casa.",
    "grand_funk" to "Potencia americana sin adornos: grave con cuerpo, guitarra al frente y la sala respirando alrededor.",
    "led_zeppelin" to "Pegada enorme y aire entre instrumentos. La dinámica cruda manda; IVANNA solo le da escenario.",
    "budgie" to "Riff denso y agudos con filo controlado: peso abajo, brillo sin aspereza arriba.",
    "edgar_winter" to "Teclado y sax con energía instrumental directa: centro sólido y colas que se abren sin perder el golpe.",
    "steve_miller" to "Guitarra limpia con espacio alrededor de cada nota. El perfil más abierto de la selección.",
    "bachman_turner_overdrive" to "Empuje constante y coros grandes: rock directo, sin distracciones, con el grave empujando parejo."
)

/** Etiqueta de carácter de cada perfil, verbatim del prototipo (HTML). */
private val prototypeProfileTags = mapOf(
    "rush" to "Precisión · Prog",
    "grand_funk" to "Potencia · 70s",
    "led_zeppelin" to "Dinámica · Aire",
    "budgie" to "Riff · Filo",
    "edgar_winter" to "Instrumental",
    "steve_miller" to "Espacio · Vuelo",
    "bachman_turner_overdrive" to "Empuje · Directo"
)

/** Ficha completa de un perfil: descripción del prototipo + ficha de pantalla. */
private fun profileSpec(profile: IvannaAudioProfile): SpecInfo = SpecInfo(
    title = prototypeShortName(profile.id) ?: profile.name,
    what = prototypeProfileDescriptions[profile.id] ?: ExperienceScreenSpec.what,
    purpose = ExperienceScreenSpec.purpose,
    whenUse = ExperienceScreenSpec.whenUse,
    whenAvoid = ExperienceScreenSpec.whenAvoid
)

private fun romanNumeral(n: Int): String = when (n) {
    1 -> "I"
    2 -> "II"
    3 -> "III"
    4 -> "IV"
    5 -> "V"
    6 -> "VI"
    7 -> "VII"
    else -> n.toString()
}

/**
 * Tarjeta del perfil destacado (prototipo, Pantalla II): marcador
 * "Perfil activo III / VII" para Rush, descripción del prototipo y el pie
 * "Firma del perfil → Motor Ω · Ruta interna · 100 %". Solo muestra: la
 * selección sigue viviendo en las tarjetas de la parrilla.
 */
@Composable
private fun FeaturedProfileCard(
    profile: IvannaAudioProfile,
    positionLabel: String,
    modifier: Modifier = Modifier
) {
    val spec = profileSpec(profile)
    Column(
        modifier = modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(16.dp))
            .background(MagistralPanel2)
            .border(1.5.dp, MagistralGold, RoundedCornerShape(16.dp))
            .padding(16.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                "PERFIL ACTIVO",
                color = MagistralGold,
                fontSize = 11.sp,
                fontWeight = FontWeight.ExtraBold,
                letterSpacing = 2.sp
            )
            Spacer(Modifier.width(8.dp))
            Text(
                positionLabel,
                color = MagistralGoldHi,
                fontSize = 11.sp,
                fontWeight = FontWeight.Bold,
                fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
            )
            Spacer(Modifier.weight(1f))
            // Dot cian: el perfil está vivo.
            Box(
                modifier = Modifier
                    .size(8.dp)
                    .clip(CircleShape)
                    .background(AuroraCyan)
            )
        }
        Spacer(Modifier.height(8.dp))
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                prototypeShortName(profile.id) ?: profile.name,
                color = MagistralGoldHi,
                fontSize = 24.sp,
                fontWeight = FontWeight.Bold,
                fontFamily = MagistralSerif,
                modifier = Modifier.weight(1f)
            )
            SpecInfoButton(spec = spec)
        }
        prototypeProfileTags[profile.id]?.let { tag ->
            Text(
                tag,
                color = MagistralMuted,
                fontSize = 11.sp,
                fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
            )
            Spacer(Modifier.height(4.dp))
        }
        Text(
            spec.what,
            color = MagistralText,
            fontSize = 13.sp,
            lineHeight = 19.sp
        )
        Spacer(Modifier.height(12.dp))
        Text(
            "Firma del perfil → Motor Ω",
            color = MagistralMuted,
            fontSize = 11.sp,
            fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
        )
        Text(
            "Ruta interna · 100 %",
            color = MagistralMuted2,
            fontSize = 11.sp,
            fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
        )
    }
}

