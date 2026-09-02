package com.ivanna.omega.ui

import android.content.Context
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.net.wifi.WifiManager
import android.telephony.TelephonyManager
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.*
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.assistant.IvannaGeminiAgent
import com.ivanna.omega.ui.theme.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

// ─── Prefs para la API Key de Gemini ────────────────────────────────────────

private const val PREFS_NETWORK = "ivanna_network_prefs"
private const val KEY_GEMINI_API = "gemini_api_key"

private fun loadGeminiKey(ctx: Context): String =
    ctx.getSharedPreferences(PREFS_NETWORK, Context.MODE_PRIVATE)
        .getString(KEY_GEMINI_API, "") ?: ""

private fun saveGeminiKey(ctx: Context, key: String) {
    ctx.getSharedPreferences(PREFS_NETWORK, Context.MODE_PRIVATE)
        .edit().putString(KEY_GEMINI_API, key).apply()
}

// ─── Estado de red ────────────────────────────────────────────────────────────

data class NetworkState(
    val hasInternet: Boolean     = false,
    val isWifi: Boolean          = false,
    val isMobile: Boolean        = false,
    val isEthernet: Boolean      = false,
    val wifiSsid: String         = "",
    val wifiRssi: Int            = -127,
    val wifiLinkMbps: Int        = 0,
    val mobileName: String       = "",
    val pingMs: Long             = -1L,
    val geminiReachable: Boolean = false
)

// ─── Monitor de red ────────────────────────────────────────────────────────────

private fun readNetworkState(ctx: Context): NetworkState {
    val cm = ctx.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
    val net = cm.activeNetwork ?: return NetworkState()
    val caps = cm.getNetworkCapabilities(net) ?: return NetworkState()

    val hasInternet = caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) &&
                      caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED)
    val isWifi     = caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)
    val isMobile   = caps.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR)
    val isEthernet = caps.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET)

    var ssid = ""; var rssi = -127; var link = 0
    if (isWifi) {
        val wm = ctx.applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
        val info = wm.connectionInfo
        ssid = info.ssid?.removeSurrounding("\"") ?: ""
        rssi = info.rssi
        link = info.linkSpeed
    }

    val mobileName = if (isMobile) {
        val tm = ctx.getSystemService(Context.TELEPHONY_SERVICE) as TelephonyManager
        runCatching { tm.networkOperatorName }.getOrElse { "" }
    } else ""

    return NetworkState(
        hasInternet = hasInternet,
        isWifi      = isWifi,
        isMobile    = isMobile,
        isEthernet  = isEthernet,
        wifiSsid    = ssid,
        wifiRssi    = rssi,
        wifiLinkMbps = link,
        mobileName  = mobileName
    )
}

private suspend fun pingHost(host: String): Long = withContext(Dispatchers.IO) {
    runCatching {
        val t0 = System.currentTimeMillis()
        val ok = Runtime.getRuntime().exec(arrayOf("ping", "-c", "1", "-W", "2", host))
            .waitFor() == 0
        if (ok) System.currentTimeMillis() - t0 else -1L
    }.getOrElse { -1L }
}

private suspend fun checkGeminiReachable(): Boolean = withContext(Dispatchers.IO) {
    runCatching {
        val url = java.net.URL("https://generativelanguage.googleapis.com/")
        val con = url.openConnection() as java.net.HttpURLConnection
        con.connectTimeout = 3000
        con.readTimeout    = 3000
        con.requestMethod  = "HEAD"
        val code = con.responseCode
        con.disconnect()
        code in 200..499   // 429 = key inválida pero server alcanzable
    }.getOrElse { false }
}

// ─── Pantalla principal ───────────────────────────────────────────────────────

@Composable
fun NetworkStatusPanel(
    modifier: Modifier = Modifier,
    onBack: () -> Unit = {}
) {
    val ctx   = LocalContext.current
    val scope = rememberCoroutineScope()

    var netState    by remember { mutableStateOf(NetworkState()) }
    var apiKey      by remember { mutableStateOf(loadGeminiKey(ctx)) }
    var keyVisible  by remember { mutableStateOf(false) }
    var apiStatus   by remember { mutableStateOf("") }   // mensaje de validación
    var connecting  by remember { mutableStateOf(false) }
    var agentLinked by remember { mutableStateOf(apiKey.isNotBlank()) }
    var lastTest    by remember { mutableStateOf("") }

    // Pulso animado del LED
    val pulse = rememberInfiniteTransition(label = "pulse")
    val pulseScale by pulse.animateFloat(
        initialValue = 0.85f, targetValue = 1.15f, label = "scale",
        animationSpec = infiniteRepeatable(tween(900, easing = FastOutSlowInEasing), RepeatMode.Reverse)
    )

    // Polling 3s de estado de red
    LaunchedEffect(Unit) {
        // Inyectar key guardada al agente al arrancar
        if (apiKey.isNotBlank()) IvannaGeminiAgent.setApiKey(apiKey)
        while (true) {
            val ns = withContext(Dispatchers.IO) { readNetworkState(ctx) }
            netState = ns
            delay(3000L)
        }
    }

    Column(
        modifier = modifier
            .fillMaxSize()
            .background(ObsidianVoid)
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 16.dp, vertical = 20.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        // ── Nav back ────────────────────────────────────────────────────────
        Text(
            "◄ VOLVER AL PANEL PRINCIPAL",
            color    = AuroraCyan,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold,
            fontSize = 14.sp,
            letterSpacing = 1.sp,
            modifier = Modifier
                .clip(RoundedCornerShape(6.dp))
                .clickable { onBack() }
                .padding(vertical = 8.dp, horizontal = 4.dp)
        )

        // ── Título ───────────────────────────────────────────────────────────
        Text(
            "CONECTIVIDAD DE RED",
            color      = AuroraCyan,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold,
            fontSize   = 14.sp,
            letterSpacing = 2.sp
        )

        // ── Estado internet ────────────────────────────────────────────────
        SectionCard(title = "ESTADO INTERNET", accentColor = if (netState.hasInternet) PhosphorGreen else CoralWarn) {
            NetRow("INTERNET",  netState.hasInternet,   "CONECTADO",     "SIN CONEXIÓN", pulseScale)
            NetRow("WiFi",      netState.isWifi,         "ACTIVO",        "OFF",           null)
            NetRow("DATOS MOV.", netState.isMobile,      "ACTIVO",        "OFF",           null)
            NetRow("ETHERNET",  netState.isEthernet,     "ACTIVO",        "OFF",           null)
        }

        // ── Detalle WiFi ───────────────────────────────────────────────────
        AnimatedVisibility(visible = netState.isWifi) {
            SectionCard(title = "DETALLE WiFi", accentColor = AuroraCyan) {
                DetailRow("SSID",    if (netState.wifiSsid.isNotBlank()) netState.wifiSsid else "—")
                DetailRow("RSSI",    "${netState.wifiRssi} dBm   ${wifiQuality(netState.wifiRssi)}")
                DetailRow("ENLACE",  "${netState.wifiLinkMbps} Mbps")
            }
        }

        // ── Detalle datos móviles ──────────────────────────────────────────
        AnimatedVisibility(visible = netState.isMobile) {
            SectionCard(title = "DATOS MÓVILES", accentColor = AmberSignal) {
                DetailRow("OPERADOR", netState.mobileName.ifBlank { "—" })
            }
        }

        // ── Test de latencia ───────────────────────────────────────────────
        SectionCard(title = "DIAGNÓSTICO", accentColor = NeonMagenta) {
            Text(
                lastTest.ifBlank { "Toca TEST para medir latencia y alcanzabilidad Gemini." },
                color      = if (lastTest.startsWith("✅")) PhosphorGreen
                             else if (lastTest.startsWith("❌") || lastTest.startsWith("⚠")) CoralWarn
                             else TextSecondary,
                fontFamily = FontFamily.Monospace,
                fontSize   = 11.sp
            )
            Spacer(Modifier.height(8.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                NetActionButton(
                    label   = "TEST RED",
                    enabled = !connecting && netState.hasInternet,
                    modifier = Modifier.weight(1f)
                ) {
                    scope.launch {
                        connecting = true
                        lastTest   = "Midiendo…"
                        val ping   = pingHost("8.8.8.8")
                        val gemini = checkGeminiReachable()
                        netState = netState.copy(pingMs = ping, geminiReachable = gemini)
                        lastTest = buildString {
                            append(if (ping >= 0) "✅ Ping Google: ${ping}ms\n" else "❌ Ping Google: timeout\n")
                            append(if (gemini) "✅ Gemini API: alcanzable" else "❌ Gemini API: no alcanzable")
                        }
                        connecting = false
                    }
                }
                NetActionButton(
                    label   = "TEST GEMINI",
                    enabled = !connecting,
                    modifier = Modifier.weight(1f)
                ) {
                    scope.launch {
                        connecting = true
                        lastTest   = "Comprobando Gemini…"
                        val ok = checkGeminiReachable()
                        netState = netState.copy(geminiReachable = ok)
                        lastTest = if (ok) "✅ Gemini API: servidor alcanzable"
                                   else    "❌ Gemini API: sin acceso (¿red bloqueada?)"
                        connecting = false
                    }
                }
            }
        }

        // ── Gemini API key + conectar al agente ────────────────────────────
        SectionCard(title = "MOTOR GEMINI (IVANNA AGENT)", accentColor = NeonMagenta) {
            // Indicador de vinculación
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                modifier = Modifier.fillMaxWidth()
            ) {
                Box(
                    Modifier
                        .size(10.dp)
                        .scale(if (agentLinked) pulseScale else 1f)
                        .clip(CircleShape)
                        .background(if (agentLinked) PhosphorGreen else CoralWarn)
                )
                Text(
                    if (agentLinked) "AGENTE CONECTADO — Gemini activo"
                    else             "AGENTE DESCONECTADO — ingresa API Key",
                    color      = if (agentLinked) PhosphorGreen else CoralWarn,
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.Bold,
                    fontSize   = 11.sp
                )
            }

            Spacer(Modifier.height(10.dp))

            // Campo API Key
            OutlinedTextField(
                value         = apiKey,
                onValueChange = { apiKey = it },
                label         = { Text("API Key de Gemini", fontSize = 11.sp) },
                placeholder   = { Text("AIzaSy…", fontSize = 11.sp, color = TextMuted) },
                singleLine    = true,
                visualTransformation = if (keyVisible) VisualTransformation.None
                                       else PasswordVisualTransformation(),
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password),
                trailingIcon  = {
                    Text(
                        if (keyVisible) "OCULTAR" else "VER",
                        color    = AuroraCyan,
                        fontSize = 10.sp,
                        modifier = Modifier
                            .clickable { keyVisible = !keyVisible }
                            .padding(end = 8.dp)
                    )
                },
                colors = OutlinedTextFieldDefaults.colors(
                    focusedBorderColor   = AuroraCyan,
                    unfocusedBorderColor = ObsidianEdge,
                    focusedLabelColor    = AuroraCyan,
                    cursorColor          = AuroraCyan,
                    focusedTextColor     = TextPrimary,
                    unfocusedTextColor   = TextSecondary
                ),
                modifier = Modifier.fillMaxWidth()
            )

            // Estado de validación
            AnimatedVisibility(visible = apiStatus.isNotBlank()) {
                Text(
                    apiStatus,
                    color      = if (apiStatus.startsWith("✅")) PhosphorGreen
                                 else if (apiStatus.startsWith("⏳")) AuroraCyan
                                 else CoralWarn,
                    fontFamily = FontFamily.Monospace,
                    fontSize   = 10.sp,
                    modifier   = Modifier.padding(top = 4.dp)
                )
            }

            Spacer(Modifier.height(10.dp))

            // Botón CONECTAR AL AGENTE
            Button(
                onClick = {
                    val trimmed = apiKey.trim()
                    if (trimmed.isBlank()) {
                        apiStatus   = "❌ Ingresa una API Key válida"
                        agentLinked = false
                        return@Button
                    }
                    scope.launch {
                        connecting  = true
                        apiStatus   = "⏳ Validando key con Gemini…"
                        val reachable = withContext(Dispatchers.IO) { checkGeminiReachable() }
                        if (!reachable) {
                            apiStatus   = "❌ Gemini no alcanzable — revisa tu red"
                            connecting  = false
                            agentLinked = false
                            return@launch
                        }
                        // Inyectar al agente y hacer un ping real
                        IvannaGeminiAgent.setApiKey(trimmed)
                        val (reply, _) = withContext(Dispatchers.IO) {
                            runCatching {
                                IvannaGeminiAgent.processQuery("ping", "test")
                            }.getOrElse { "error" to null }
                        }
                        val ok = reply != "error" && !reply.toString().contains("error", ignoreCase = true)
                        if (ok) {
                            saveGeminiKey(ctx, trimmed)
                            agentLinked = true
                            apiStatus   = "✅ Agente IVANNA vinculado — Gemini respondió OK"
                        } else {
                            // key inválida o cuota agotada — guardar de todas formas
                            // para que el agente use fallback simulado
                            saveGeminiKey(ctx, trimmed)
                            IvannaGeminiAgent.setApiKey(trimmed)
                            agentLinked = true
                            apiStatus   = "⚠ Key guardada — el agente usará modo híbrido (simulado + Gemini)"
                        }
                        connecting = false
                    }
                },
                enabled  = !connecting,
                modifier = Modifier
                    .fillMaxWidth()
                    .height(52.dp),
                colors   = ButtonDefaults.buttonColors(
                    containerColor = if (agentLinked) PhosphorGreen.copy(0.18f) else NeonMagenta.copy(0.18f),
                    contentColor   = if (agentLinked) PhosphorGreen else NeonMagenta
                ),
                shape    = RoundedCornerShape(14.dp),
                border   = androidx.compose.foundation.BorderStroke(
                    1.dp,
                    if (agentLinked) PhosphorGreen.copy(0.6f) else NeonMagenta.copy(0.6f)
                )
            ) {
                if (connecting) {
                    CircularProgressIndicator(
                        modifier = Modifier.size(18.dp),
                        color    = NeonMagenta,
                        strokeWidth = 2.dp
                    )
                    Spacer(Modifier.width(8.dp))
                }
                Text(
                    if (agentLinked) "✓ AGENTE VINCULADO — RECONECTAR" else "CONECTAR AL AGENTE IVANNA",
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.Bold,
                    fontSize   = 12.sp
                )
            }

            Spacer(Modifier.height(6.dp))

            // Botón desconectar
            AnimatedVisibility(visible = agentLinked) {
                OutlinedButton(
                    onClick  = {
                        saveGeminiKey(ctx, "")
                        IvannaGeminiAgent.setApiKey("API_KEY_PLACEHOLDER")
                        agentLinked = false
                        apiStatus   = "Agente desconectado — modo simulado activo"
                        apiKey      = ""
                    },
                    modifier = Modifier.fillMaxWidth().height(40.dp),
                    colors   = ButtonDefaults.outlinedButtonColors(contentColor = CoralWarn),
                    shape    = RoundedCornerShape(10.dp),
                    border   = androidx.compose.foundation.BorderStroke(1.dp, CoralWarn.copy(0.5f))
                ) { Text("DESCONECTAR AGENTE", fontSize = 11.sp, fontFamily = FontFamily.Monospace) }
            }

            Spacer(Modifier.height(8.dp))
            Text(
                "La API Key se guarda localmente en el dispositivo.\n" +
                "Obtén una gratis en: aistudio.google.com/apikey",
                color    = TextMuted,
                fontSize = 9.sp
            )
        }

        // ── Resumen de conectividad ────────────────────────────────────────
        if (netState.pingMs >= 0 || netState.geminiReachable) {
            SectionCard(title = "ÚLTIMO DIAGNÓSTICO", accentColor = AuroraCyan) {
                if (netState.pingMs >= 0)
                    DetailRow("Ping 8.8.8.8",  "${netState.pingMs} ms")
                DetailRow("Gemini API",
                    if (netState.geminiReachable) "✅ Alcanzable" else "❌ Sin acceso")
            }
        }
    }
}

// ─── Composables auxiliares ────────────────────────────────────────────────────

@Composable
private fun SectionCard(
    title: String,
    accentColor: Color,
    content: @Composable ColumnScope.() -> Unit
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(ObsidianSoft.copy(alpha = 0.7f), RoundedCornerShape(12.dp))
            .border(1.dp, accentColor.copy(alpha = 0.35f), RoundedCornerShape(12.dp))
            .padding(14.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
        content = {
            Text(
                title,
                color      = accentColor,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold,
                fontSize   = 11.sp,
                letterSpacing = 1.5.sp
            )
            content()
        }
    )
}

@Composable
private fun NetRow(
    label: String,
    active: Boolean,
    activeText: String,
    inactiveText: String,
    pulseScale: Float?
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(10.dp)
    ) {
        Text(
            label,
            color      = Color.White.copy(alpha = 0.5f),
            fontFamily = FontFamily.Monospace,
            fontSize   = 11.sp,
            modifier   = Modifier.width(90.dp)
        )
        Box(
            Modifier
                .size(8.dp)
                .scale(if (active && pulseScale != null) pulseScale else 1f)
                .clip(CircleShape)
                .background(if (active) PhosphorGreen else CoralWarn)
        )
        Text(
            if (active) activeText else inactiveText,
            color      = if (active) PhosphorGreen else CoralWarn,
            fontFamily = FontFamily.Monospace,
            fontSize   = 12.sp,
            fontWeight = FontWeight.Bold
        )
    }
}

@Composable
private fun DetailRow(label: String, value: String) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(label, color = TextSecondary, fontFamily = FontFamily.Monospace, fontSize = 10.sp)
        Text(value, color = TextPrimary,   fontFamily = FontFamily.Monospace, fontSize = 10.sp,
            fontWeight = FontWeight.Bold)
    }
}

@Composable
private fun NetActionButton(
    label: String,
    enabled: Boolean,
    modifier: Modifier = Modifier,
    onClick: () -> Unit
) {
    Box(
        modifier = modifier
            .clip(RoundedCornerShape(8.dp))
            .background(if (enabled) AuroraCyan.copy(0.18f) else ObsidianEdge.copy(0.4f))
            .border(1.dp, if (enabled) AuroraCyan else ObsidianEdge, RoundedCornerShape(8.dp))
            .clickable(enabled = enabled, onClick = onClick)
            .padding(vertical = 10.dp),
        contentAlignment = Alignment.Center
    ) {
        Text(
            label,
            color      = if (enabled) AuroraCyan else TextMuted,
            fontFamily = FontFamily.Monospace,
            fontSize   = 11.sp,
            fontWeight = FontWeight.Bold
        )
    }
}

private fun wifiQuality(rssi: Int) = when {
    rssi >= -55 -> "🟢 Excelente"
    rssi >= -65 -> "🟡 Buena"
    rssi >= -75 -> "🟠 Regular"
    else        -> "🔴 Débil"
}
