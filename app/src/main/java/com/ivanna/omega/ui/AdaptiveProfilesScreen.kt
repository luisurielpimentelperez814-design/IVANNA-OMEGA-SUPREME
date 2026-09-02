package com.ivanna.omega.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.audio.IvannaAudioProfile
import com.ivanna.omega.audio.ProfilesLoader
import com.ivanna.omega.ui.theme.*

// ═══════════════════════════════════════════════════════════════════════════
//  ADAPTIVE PROFILES SCREEN — RESTAURADA (fix build 2026-08-11)
//
//  Historia: la ruta "adaptive_profiles" quedó cableada en MainActivity,
//  IvannaControlPanel y ControlTabScreen, pero el composable destino nunca
//  llegó a main (el archivo se perdió fuera del árbol en la limpieza de
//  código muerto). El build fallaba con:
//    MainActivity.kt:452  Unresolved reference: AdaptiveProfilesScreen
//
//  REGLAS DE CONEXIÓN (mismas que ProfileSelectorScreen v3.0):
//    - Fuente de datos real: ProfilesLoader.load(context) lee
//      res/raw/audio_profiles.json. Si el recurso no existe o el JSON no
//      parsea, la pantalla muestra estado vacío — NUNCA perfiles inventados.
//    - La selección NO aplica el perfil directamente: la pantalla es solo
//      navegación/consulta desde el Control Panel. La aplicación de perfiles
//      sigue siendo responsabilidad del flujo PROFILES (ProfileSelector →
//      onApply → MainActivity → DSP), que ya está cableado (commit 4e77fac).
//    - "Modo adaptativo" es información de estado del engine
//      (AdaptiveDecisionEngine) — esta pantalla no lo modifica; para eso
//      está el Adaptive Control Center (AdaptiveEngineCard).
// ═══════════════════════════════════════════════════════════════════════════

@Composable
fun AdaptiveProfilesScreen(
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current

    // Carga perezosa una sola vez por composición; ProfilesLoader ya valida
    // el JSON (ignoreUnknownKeys + coerceInputValues) y devuelve lista vacía
    // si el recurso falta.
    val profiles by remember {
        mutableStateOf(runCatching { ProfilesLoader.load(context) }.getOrDefault(emptyList()))
    }
    val metadata by remember {
        mutableStateOf(runCatching { ProfilesLoader.loadMetadata(context) }.getOrNull())
    }

    // FIX (tarjetas decorativas): AdaptiveProfileCard no tenía acción — se
    // veía el perfil (con parámetros DSP reales en el modelo) pero pulsarlo
    // no hacía nada. Ahora aplica de verdad vía ProfileManagerBridge →
    // ProfileManager.applyToDsp → DSPStatePrefs (mismo path que
    // ProfileSelectorScreen) y persiste el id activo.
    val bridge = remember { com.ivanna.omega.ProfileManagerBridge(context) }
    // Mismo store que usa MainActivity/ProfileSelectorScreen (ParameterStore,
    // claves compartidas) — no un store nuevo que desalinearía el id activo.
    val profileStore = remember { com.ivanna.omega.core.ParameterStore(context) }
    var appliedId by remember {
        mutableStateOf(runCatching { profileStore.getCurrentAudioProfileId() }.getOrNull())
    }
    var dsp by remember {
        mutableStateOf(com.ivanna.omega.dsp.DSPStatePrefs.load(context))
    }

    Column(
        modifier = modifier
            .background(ObsidianDeep)
            .padding(horizontal = 20.dp, vertical = 16.dp)
    ) {
        // ── Encabezado ──────────────────────────────────────────────────────
        Text(
            "PERFILES ADAPTATIVOS",
            color = TextPrimary,
            fontSize = 18.sp,
            fontWeight = FontWeight.ExtraBold,
            letterSpacing = 2.sp
        )
        Text(
            metadata?.let { "${it.totalProfiles} perfiles · v${it.version}" }
                ?: "${profiles.size} perfiles cargados",
            color = TextSecondary,
            fontSize = 11.sp,
            letterSpacing = 0.5.sp,
            modifier = Modifier.padding(top = 4.dp)
        )

        Spacer(Modifier.height(16.dp))

        if (profiles.isEmpty()) {
            // Estado vacío honesto: el recurso no está o no parseó.
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(12.dp))
                    .background(ObsidianSoft)
                    .border(1.dp, ObsidianEdge, RoundedCornerShape(12.dp))
                    .padding(24.dp),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    "No se pudieron cargar los perfiles.\nVerifica res/raw/audio_profiles.json.",
                    color = TextMuted,
                    fontSize = 13.sp,
                    textAlign = TextAlign.Center
                )
            }
            return@Column
        }

        LazyColumn(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            items(profiles, key = { it.id }) { profile ->
                AdaptiveProfileCard(
                    profile   = profile,
                    isApplied = profile.id == appliedId,
                    onApply   = {
                        bridge.applyProfile(profile.id, dsp) { updated ->
                            dsp = updated
                        }
                        appliedId = profile.id
                        profileStore.setCurrentAudioProfileId(profile.id)
                    }
                )
            }
        }
    }
}

@Composable
private fun AdaptiveProfileCard(
    profile: IvannaAudioProfile,
    isApplied: Boolean = false,
    onApply: () -> Unit = {}
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .background(if (isApplied) ObsidianSoft else ObsidianSoft)
            .border(
                1.dp,
                if (isApplied) AuroraCyan else ObsidianEdge,
                RoundedCornerShape(12.dp)
            )
            .clickable { onApply() }
            .padding(14.dp)
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                profile.name,
                color = TextPrimary,
                fontSize = 14.sp,
                fontWeight = FontWeight.Bold,
                modifier = Modifier.weight(1f)
            )
            Text(
                profile.category.uppercase(),
                color = AuroraCyan,
                fontSize = 9.sp,
                fontWeight = FontWeight.SemiBold,
                letterSpacing = 1.sp,
                modifier = Modifier
                    .clip(RoundedCornerShape(4.dp))
                    .background(ObsidianDeep)
                    .padding(horizontal = 6.dp, vertical = 2.dp)
            )
        }
        Text(
            profile.description,
            color = TextSecondary,
            fontSize = 11.sp,
            modifier = Modifier.padding(top = 6.dp)
        )
        if (profile.recommendedFor.isNotBlank()) {
            Text(
                "Recomendado: ${profile.recommendedFor}",
                color = TextMuted,
                fontSize = 10.sp,
                modifier = Modifier.padding(top = 4.dp)
            )
        }
        if (profile.tags.isNotEmpty()) {
            Row(
                modifier = Modifier.padding(top = 8.dp),
                horizontalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                profile.tags.take(4).forEach { tag ->
                    Text(
                        tag,
                        color = TextMuted,
                        fontSize = 9.sp,
                        modifier = Modifier
                            .clip(RoundedCornerShape(4.dp))
                            .border(1.dp, ObsidianEdge, RoundedCornerShape(4.dp))
                            .padding(horizontal = 5.dp, vertical = 1.dp)
                    )
                }
            }
        }
    }
}
