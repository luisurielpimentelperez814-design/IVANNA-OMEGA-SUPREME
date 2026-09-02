package com.ivanna.omega.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.platform.LocalContext
import android.content.Context
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.magisk.OmegaEngineBridge
import com.ivanna.omega.ui.theme.*

/**
 * SystemScreen — Sección SISTEMA unificada.
 * Reemplaza: MagiskStatusPanel + ProfileSelectorScreen + TelemetryDashboard
 * en una sola pantalla con 3 tabs.
 */
@Composable
fun SystemScreen(
    modifier: Modifier = Modifier,
    onOpenMagisk: () -> Unit = {},
    onOpenProfiles: () -> Unit = {},
    onOpenNetwork: () -> Unit = {}
) {
    var selectedTab by remember { mutableIntStateOf(0) }
    val tabs = listOf("MAGISK", "PERFILES", "TELEMETRÍA", "RED")

    Column(modifier = modifier.background(ObsidianDeep)) {
        Text(
            "SISTEMA",
            color = PhosphorGreen,
            fontSize = 11.sp,
            fontWeight = FontWeight.ExtraBold,
            letterSpacing = 3.sp,
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp)
        )
        ScrollableTabRow(
            selectedTabIndex = selectedTab,
            containerColor = Color.Transparent,
            contentColor = PhosphorGreen,
            edgePadding = 16.dp
        ) {
            tabs.forEachIndexed { i, title ->
                Tab(
                    selected = selectedTab == i,
                    onClick  = { selectedTab = i },
                    text = {
                        Text(
                            title, fontSize = 11.sp,
                            fontWeight = if (selectedTab == i) FontWeight.Bold else FontWeight.Normal,
                            color = if (selectedTab == i) PhosphorGreen else TextMuted
                        )
                    }
                )
            }
        }
        HorizontalDivider(color = ObsidianEdge, thickness = 0.5.dp)
        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            when (selectedTab) {
                0 -> MagiskTab(onOpenMagisk)
                1 -> ProfilesTab(onOpenProfiles)
                2 -> TelemetryTab()
                3 -> NetworkTab(onOpenNetwork)
            }
        }
    }
}

@Composable
private fun MagiskTab(onOpenMagisk: () -> Unit) {
    GlassCard("MÓDULO MAGISK", PhosphorGreen, "ivanna_omega · libomega_effect.so · daemon") {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            // Bug A fix real: isLoaded es Boolean plano, no StateFlow.
            // derivedStateOf no disparaba recomposición porque la fuente
            // no es estado Compose. produceState con polling 200ms garantiza
            // que el LED refleje cambios reales en isLoaded.
            val loaded by produceState(initialValue = IvannaNativeLib.isLoaded) {
                while (true) {
                    value = IvannaNativeLib.isLoaded
                    kotlinx.coroutines.delay(200)
                }
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(
                    Modifier.size(8.dp)
                        .background(if (loaded) PhosphorGreen else Color.Red, androidx.compose.foundation.shape.CircleShape)
                )
                Spacer(Modifier.width(8.dp))
                Text(
                    if (loaded) "Módulo cargado · JNI activo" else "Módulo no detectado",
                    color = if (loaded) PhosphorGreen else Color.Red,
                    fontSize = 12.sp
                )
            }
            listOf(
                "libivanna_omega.so" to loaded,
                "libomega_effect.so" to loaded,
                "ivanna_daemon"      to loaded
            ).forEach { (name, ok) ->
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text(name, color = TextSecondary, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
                    Text(if (ok) "OK" else "MISSING", color = if (ok) PhosphorGreen else Color.Red, fontSize = 11.sp)
                }
            }
            Spacer(Modifier.height(4.dp))
            OutlinedButton(
                onClick = onOpenMagisk,
                modifier = Modifier.fillMaxWidth(),
                colors = ButtonDefaults.outlinedButtonColors(contentColor = PhosphorGreen)
            ) { Text("VER PANEL MAGISK COMPLETO", fontSize = 11.sp) }
        }
    }
}

// Parámetros DSP de cada preset: (low, mid, high, presence, master, exciterWet, stereoWidth, compThresholdDb, compRatio)
private val PRESET_PARAMS = mapOf(
    "CÁLIDO"   to floatArrayOf(+4f, +1f, -2f,  0f,  0f, 0.25f, 1.0f, -24f, 3f),
    "NATURAL"  to floatArrayOf( 0f,  0f,  0f,  0f,  0f, 0.10f, 1.0f, -20f, 2f),
    "ESPACIAL" to floatArrayOf( 0f, +1f, +2f, +2f,  0f, 0.30f, 1.5f, -18f, 2f),
    "VOCAL"    to floatArrayOf(-2f, +3f, +1f, +4f,  0f, 0.15f, 0.9f, -16f, 4f),
    "EXTREMO"  to floatArrayOf(+6f, +3f, +4f, +3f, +3f, 0.60f, 1.4f, -12f, 6f)
)

@Composable
private fun ProfilesTab(onOpenProfiles: () -> Unit) {
    val context = LocalContext.current
    // FIX: los botones de perfil sólo llamaban onOpenProfiles (navegar a otra
    // pantalla) sin tocar ningún parámetro de audio — eran botones de navegación
    // disfrazados de presets. Ahora cada botón aplica sus valores DSP al motor
    // nativo (DSPBridge, IvannaNativeLib, OmegaEngineBridge) y persiste la
    // selección en AdaptiveControlsPrefs.
    var selected by remember {
        mutableStateOf(AdaptiveControlsPrefs.load(context).selectedPreset)
    }
    GlassCard("PERFILES DE AUDIO", AuroraCyan, "Presets aplicados al DSP en tiempo real") {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            listOf(
                "CÁLIDO"   to "Graves realzados, agudos suaves",
                "NATURAL"  to "Curva plana, respuesta neutra",
                "ESPACIAL" to "HRTF + widener máximo",
                "VOCAL"    to "Presencia 2.5 kHz, menos graves",
                "EXTREMO"  to "Todo al máximo · Material denso"
            ).forEach { (name, desc) ->
                val isSelected = selected == name
                OutlinedButton(
                    onClick = {
                        selected = name
                        AdaptiveControlsPrefs.save(
                            context,
                            AdaptiveControlsPrefs.load(context).copy(selectedPreset = name)
                        )
                        // Aplicar parámetros DSP reales del preset
                        PRESET_PARAMS[name]?.let { p ->
                            val low = p[0]; val mid = p[1]; val high = p[2]
                            val presence = p[3]; val master = p[4]
                            val exciterWet = p[5]; val stereoWidth = p[6]
                            val compThreshDb = p[7]; val compRatio = p[8]
                            if (DSPBridge.isLoaded) runCatching {
                                DSPBridge.setParams(
                                    // FIX: mix=0.70→0.5 (neutro). mix>0.5 → pre-EQ gain → biquad inestable
                                    drive = 0.45f, wet = exciterWet, mix = 0.5f,
                                    alpha = ((compThreshDb + 24f) / 24f).coerceIn(0f, 1f),
                                    beta  = ((compRatio - 1f) / 19f).coerceIn(0f, 1f),
                                    gamma = 0.72f, freq = 1000f, resonance = 0.707f,
                                    low = low, mid = mid, high = high,
                                    presence = presence, master = master
                                )
                                DSPBridge.setStereoWidth(stereoWidth)
                            }
                            if (IvannaNativeLib.isLoaded) runCatching {
                                IvannaNativeLib.nativeSetEQParams(low, mid, high, master)
                                IvannaNativeLib.nativeSetCompressorParams(compThreshDb, compRatio, 10f, 100f)
                            }
                            runCatching {
                                OmegaEngineBridge.sendPerceptualState(
                                    compressor     = compThreshDb / 60f,
                                    exciterRed     = exciterWet,
                                    highCut        = 19500f,
                                    spatialWidth   = stereoWidth,
                                    loudnessTarget = -16f,
                                    harmonicGain   = exciterWet,
                                    antiDolby      = 0f
                                )
                            }
                        }
                    },
                    modifier = Modifier.fillMaxWidth(),
                    border = androidx.compose.foundation.BorderStroke(
                        if (isSelected) 1.5.dp else 0.5.dp,
                        if (isSelected) AuroraCyan else ObsidianEdge
                    ),
                    colors = ButtonDefaults.outlinedButtonColors(
                        containerColor = if (isSelected) AuroraCyan.copy(alpha = 0.12f) else androidx.compose.ui.graphics.Color.Transparent,
                        contentColor = if (isSelected) AuroraCyan else TextSecondary
                    )
                ) {
                    Column {
                        Text(name, fontSize = 12.sp, fontWeight = FontWeight.Bold)
                        Text(desc, fontSize = 10.sp, color = TextMuted)
                    }
                }
            }
            Spacer(Modifier.height(4.dp))
            OutlinedButton(
                onClick = onOpenProfiles,
                modifier = Modifier.fillMaxWidth(),
                colors = ButtonDefaults.outlinedButtonColors(contentColor = NeonMagenta)
            ) { Text("VER TODOS LOS PERFILES →", fontSize = 11.sp) }
        }
    }
}

@Composable
private fun TelemetryTab() {
    var telemetry by remember { mutableStateOf<FloatArray?>(null) }
    // v2.3.0: telemetría térmica + DSP tier via prop de sistema
    var thermalTempC  by remember { mutableStateOf("--") }
    var thermalTier   by remember { mutableStateOf("--") }
    var dspQuality    by remember { mutableStateOf("100%") }
    var rirActive     by remember { mutableStateOf(true) }

    LaunchedEffect(Unit) {
        while (true) {
            if (IvannaNativeLib.isLoaded)
                telemetry = runCatching { IvannaNativeLib.nativeGetAdaptiveTelemetry() }.getOrNull()

            // Leer estado térmico desde props de sistema (escritas por ThermalGovernor vía JNI)
            // El ThermalGovernor escribe persist.ivanna.thermal_tier y persist.ivanna.thermal_temp
            val tierProp = runCatching {
                Runtime.getRuntime().exec("getprop persist.ivanna.thermal_tier")
                    .inputStream.bufferedReader().readLine()?.trim() ?: "0"
            }.getOrElse { "0" }

            val tierInt = tierProp.toIntOrNull() ?: 0
            thermalTier = when (tierInt) {
                0 -> "FULL ✅"
                1 -> "REDUCED ⚡"
                2 -> "LIMITED 🌡️"
                3 -> "PROTECTED 🔥"
                4 -> "BYPASS ❌"
                else -> "UNKNOWN"
            }
            thermalTempC = when (tierInt) {
                0 -> "< 45°C"
                1 -> "45-55°C"
                2 -> "55-65°C"
                3 -> "65-75°C"
                4 -> "> 75°C"
                else -> "--"
            }
            dspQuality = when (tierInt) {
                0 -> "100%"
                1 -> "85%"
                2 -> "65%"
                3 -> "40%"
                4 -> "0% (bypass)"
                else -> "--"
            }
            rirActive = tierInt < 2

            kotlinx.coroutines.delay(2000) // cada 2s — mismo intervalo que ThermalGovernor
        }
    }

    // ── Card térmica — estado real del SoC ───────────────────────────────────
    val tierColor = when (thermalTier) {
        "FULL ✅"       -> PhosphorGreen
        "REDUCED ⚡"   -> AuroraCyan
        "LIMITED 🌡️"  -> AmberSignal
        "PROTECTED 🔥" -> NeonMagenta
        else            -> Color.Red
    }
    GlassCard("ESTADO TÉRMICO DSP", tierColor, "ThermalGovernor v2.3.0 · polling 2s") {
        Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Text("SoC Temperatura", color = TextSecondary, fontSize = 10.sp)
                Text(thermalTempC, color = tierColor, fontSize = 10.sp, fontWeight = FontWeight.Bold)
            }
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Text("Tier Térmico", color = TextSecondary, fontSize = 10.sp)
                Text(thermalTier, color = tierColor, fontSize = 10.sp, fontWeight = FontWeight.Bold)
            }
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Text("Calidad DSP", color = TextSecondary, fontSize = 10.sp)
                Text(dspQuality, color = tierColor, fontSize = 10.sp, fontWeight = FontWeight.Bold)
            }
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Text("RIR Convolución", color = TextSecondary, fontSize = 10.sp)
                Text(
                    if (rirActive) "ACTIVA" else "SUSPENDIDA (calor)",
                    color = if (rirActive) PhosphorGreen else NeonMagenta,
                    fontSize = 10.sp, fontWeight = FontWeight.Bold
                )
            }
            Spacer(Modifier.height(4.dp))
            Text(
                "FULL: DSP completo | REDUCED: sin Volterra | LIMITED: sin RIR | BYPASS: pass-through",
                color = TextMuted, fontSize = 8.sp
            )
        }
    }

    Spacer(Modifier.height(8.dp))

    // ── Card de telemetría de audio ───────────────────────────────────────────
    GlassCard("TELEMETRÍA EN VIVO", PhosphorGreen, "10Hz · Motor A · Pipeline completo") {
        val labels = listOf("RMS","Peak dB","GR dB","CPU %","Target","CompAmt","ExcRed","SpatW","VoiceProt","Running")
        val colors = listOf(AuroraCyan, Color.Red, NeonMagenta, PhosphorGreen,
            AuroraCyan, NeonMagenta, AuroraCyan, PhosphorGreen, NeonMagenta, AuroraCyan)
        Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
            telemetry?.forEachIndexed { i, v ->
                if (i < labels.size) {
                    val c = colors[i]
                    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                        Text(labels[i], color = TextSecondary, fontSize = 10.sp)
                        Text("%.4f".format(v), color = c, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
                    }
                    LinearProgressIndicator(
                        progress = { v.coerceIn(0f, 1f) },
                        modifier = Modifier.fillMaxWidth().height(2.dp),
                        color = c, trackColor = ObsidianEdge
                    )
                }
            } ?: Text("Cargando...", color = TextMuted, fontSize = 11.sp)
        }
    }
}

// ── NetworkTab ────────────────────────────────────────────────────────────────

@Composable
private fun NetworkTab(onOpenNetwork: () -> Unit) {
    val ctx = LocalContext.current

    val hasInternet by produceState(initialValue = false) {
        while (true) {
            value = runCatching {
                val cm = ctx.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
                val net = cm.activeNetwork ?: return@runCatching false
                val caps = cm.getNetworkCapabilities(net) ?: return@runCatching false
                caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) &&
                caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED)
            }.getOrElse { false }
            kotlinx.coroutines.delay(3000)
        }
    }

    val isWifi by produceState(initialValue = false) {
        while (true) {
            value = runCatching {
                val cm = ctx.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
                val caps = cm.getNetworkCapabilities(cm.activeNetwork ?: return@runCatching false) ?: return@runCatching false
                caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)
            }.getOrElse { false }
            kotlinx.coroutines.delay(3000)
        }
    }

    val isMobile by produceState(initialValue = false) {
        while (true) {
            value = runCatching {
                val cm = ctx.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
                val caps = cm.getNetworkCapabilities(cm.activeNetwork ?: return@runCatching false) ?: return@runCatching false
                caps.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR)
            }.getOrElse { false }
            kotlinx.coroutines.delay(3000)
        }
    }

    val agentLinked = runCatching {
        ctx.getSharedPreferences("ivanna_network_prefs", Context.MODE_PRIVATE)
            .getString("gemini_api_key", "").orEmpty().isNotBlank()
    }.getOrElse { false }

    GlassCard("RED & MOTOR GEMINI", AuroraCyan, "WiFi · Datos · API Key · Agente IVANNA") {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            listOf(
                Triple("INTERNET",   hasInternet, if (hasInternet) "CONECTADO" else "SIN RED"),
                Triple("WiFi",       isWifi,      if (isWifi)      "ACTIVO"    else "OFF"),
                Triple("DATOS MOV.", isMobile,    if (isMobile)    "ACTIVO"    else "OFF")
            ).forEach { (label, active, text) ->
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Box(Modifier.size(8.dp).clip(CircleShape).background(if (active) PhosphorGreen else CoralWarn))
                    Text(label, color = TextSecondary, fontSize = 10.sp,
                        fontFamily = FontFamily.Monospace, modifier = Modifier.width(82.dp))
                    Text(text, color = if (active) PhosphorGreen else CoralWarn,
                        fontSize = 10.sp, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold)
                }
            }

            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                modifier = Modifier.fillMaxWidth()
            ) {
                Box(Modifier.size(8.dp).clip(CircleShape).background(if (agentLinked) PhosphorGreen else AmberSignal))
                Text("AGENTE", color = TextSecondary, fontSize = 10.sp,
                    fontFamily = FontFamily.Monospace, modifier = Modifier.width(82.dp))
                Text(if (agentLinked) "GEMINI ACTIVO" else "SIN API KEY",
                    color = if (agentLinked) PhosphorGreen else AmberSignal,
                    fontSize = 10.sp, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold)
            }

            Spacer(Modifier.height(4.dp))

            Button(
                onClick = onOpenNetwork,
                modifier = Modifier.fillMaxWidth(),
                colors = ButtonDefaults.buttonColors(
                    containerColor = AuroraCyan.copy(alpha = 0.15f),
                    contentColor   = AuroraCyan
                ),
                shape  = androidx.compose.foundation.shape.RoundedCornerShape(10.dp),
                border = androidx.compose.foundation.BorderStroke(1.dp, AuroraCyan.copy(0.5f))
            ) {
                Text(
                    if (agentLinked) "PANEL DE RED — RECONFIGURAR →" else "PANEL DE RED — CONECTAR AGENTE →",
                    fontSize = 11.sp, fontFamily = FontFamily.Monospace
                )
            }
        }
    }
}
