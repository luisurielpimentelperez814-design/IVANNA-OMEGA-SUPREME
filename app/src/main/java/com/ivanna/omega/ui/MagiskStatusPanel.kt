package com.ivanna.omega.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.magisk.MagiskBridge
import com.ivanna.omega.magisk.OmegaEngineBridge
import com.ivanna.omega.ui.theme.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * MagiskStatusPanel — pantalla de estado del módulo Magisk + cliente daemon.
 *
 * FIX CRÍTICO (socket siempre DESCONECTADO):
 *   - daemonConnected ahora refleja omegaBridge.isConnected que se actualiza
 *     con un probe real en OmegaEngineBridge.connect().
 *   - Se agregó botón RECONECTAR para disparar connect() manualmente desde IO.
 *   - El polling cada 2s también dispara connect() si no está conectado
 *     (descarga extra ligera: solo un probe de socket, no un comando completo).
 *   - Estado SOCKET: muestra N/A cuando Magisk no está instalado (no error).
 */
@Composable
fun MagiskStatusPanel(
    omegaBridge: OmegaEngineBridge,
    modifier: Modifier = Modifier,
    onBack: () -> Unit = {}
) {
    var moduleActive    by remember { mutableStateOf(false) }
    var moduleVersion   by remember { mutableStateOf("") }
    val context = androidx.compose.ui.platform.LocalContext.current
    var daemonRunning   by remember { mutableStateOf(false) }
    var daemonConnected by remember { mutableStateOf(false) }
    var lastCommandOutput by remember { mutableStateOf("") }
    val scope = rememberCoroutineScope()
    var actionInFlight by remember { mutableStateOf(false) }

    // ── Polling + auto-reconexión ────────────────────────────────────────
    LaunchedEffect(Unit) {
        while (true) {
            val (active, version, running) = withContext(Dispatchers.IO) {
                Triple(
                    MagiskBridge.isModuleActive,
                    MagiskBridge.moduleVersion,
                    MagiskBridge.isDaemonRunning
                )
            }

            // FIX: si el daemon está corriendo pero el bridge no está conectado,
            // intentar reconexión desde IO (probe real, no fake).
            val connected = withContext(Dispatchers.IO) {
                if (!omegaBridge.isConnected && (running || active)) {
                    omegaBridge.connect()
                }
                omegaBridge.isConnected
            }

            moduleActive    = active
            moduleVersion   = version
            daemonRunning   = running || connected
            daemonConnected = connected
            delay(2000L)
        }
    }

    Column(
        modifier = modifier
            .fillMaxSize()
            .background(ObsidianVoid)
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 16.dp, vertical = 20.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        // Botón de Regreso
        Row(
            modifier = Modifier.fillMaxWidth().padding(bottom = 8.dp),
            horizontalArrangement = Arrangement.Start
        ) {
            Text(
                text = "◄ VOLVER AL PANEL PRINCIPAL",
                color = AuroraCyan,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold,
                fontSize = 14.sp,
                letterSpacing = 1.sp,
                modifier = Modifier
                    .clip(RoundedCornerShape(6.dp))
                    .clickable { onBack() }
                    .padding(vertical = 8.dp, horizontal = 4.dp)
            )
        }

        Text(
            "MÓDULO MAGISK",
            color = AuroraCyan,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold,
            fontSize = 14.sp,
            letterSpacing = 2.sp
        )

        StatusRow("MÓDULO",  moduleActive, "ACTIVO", "NO INSTALADO")
        StatusRow("VERSIÓN",
            moduleVersion.isNotEmpty() && moduleVersion != "unknown",
            moduleVersion, "—")
        StatusRow("DAEMON",  daemonRunning, "CORRIENDO", "DETENIDO")

        Spacer(Modifier.height(6.dp))
        DividerGlow()

        Text(
            "OMEGA DAEMON BRIDGE",
            color = NeonMagenta,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold,
            fontSize = 14.sp,
            letterSpacing = 2.sp
        )

        // FIX: cuando no hay módulo Magisk, mostrar N/A (no error) para el socket
        if (!moduleActive && !daemonConnected) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Text("SOCKET", color = Color.White.copy(alpha = 0.5f),
                    fontFamily = FontFamily.Monospace, fontSize = 11.sp,
                    modifier = Modifier.width(82.dp))
                Box(Modifier.size(8.dp).clip(CircleShape).background(TextMuted))
                Text("N/A (sin módulo Magisk)", color = TextMuted,
                    fontFamily = FontFamily.Monospace, fontSize = 12.sp,
                    fontWeight = FontWeight.Bold)
            }
        } else {
            StatusRow("SOCKET", daemonConnected, "CONECTADO", "DESCONECTADO")
        }

        Text(
            text = "Socket principal: @omega_daemon_socket\n" +
                    "Fallback legacy:   /data/pf/pf.sock",
            color = TextSecondary,
            fontFamily = FontFamily.Monospace,
            fontSize = 10.sp
        )

        Spacer(Modifier.height(6.dp))
        DividerGlow()

        Text("ACCIONES", color = AuroraCyan, fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold, fontSize = 13.sp, letterSpacing = 2.sp)

        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            ActionButton("STATUS", daemonRunning && !actionInFlight) {
                actionInFlight = true
                scope.launch {
                    val result = withContext(Dispatchers.IO) { MagiskBridge.getStatus() }
                    lastCommandOutput = result
                    actionInFlight = false
                }
            }
            ActionButton("TELEMETRY", daemonConnected && !actionInFlight) {
                actionInFlight = true
                scope.launch {
                    val result = withContext(Dispatchers.IO) { omegaBridge.requestTelemetry() }
                    lastCommandOutput = result
                    actionInFlight = false
                }
            }
            ActionButton("RELOAD", moduleActive && !actionInFlight) {
                actionInFlight = true
                scope.launch {
                    val result = withContext(Dispatchers.IO) { MagiskBridge.reloadParams() }
                    lastCommandOutput = result
                    actionInFlight = false
                }
            }
            // FIX: botón RECONECTAR para forzar probe manual
            ActionButton("RECONECTAR", !actionInFlight) {
                actionInFlight = true
                scope.launch {
                    lastCommandOutput = "Probando socket..."
                    val ok = withContext(Dispatchers.IO) { omegaBridge.connect() }
                    daemonConnected  = ok
                    daemonRunning    = ok || daemonRunning
                    lastCommandOutput = if (ok)
                        "✅ Socket conectado. Latencia: ${omegaBridge.getLastLatencyMs()}ms"
                    else
                        "⚪ Socket no disponible (daemon offline o módulo no instalado)"
                    actionInFlight = false
                }
            }
            ActionButton("ROOT PING", !actionInFlight) {
                actionInFlight = true
                scope.launch {
                    lastCommandOutput = "Solicitando Root (Magisk)..."
                    val rootOk = withContext(Dispatchers.IO) { com.ivanna.omega.core.RootAccess.probeSu(force = true) }
                    lastCommandOutput = if (rootOk) {
                        "✅ Root concedido. Reevaluando backend..."
                    } else {
                        "❌ Root denegado o Magisk no responde."
                    }
                    if (rootOk) {
                        com.ivanna.omega.audio.AudioBackendSelector.start(context)
                    }
                    actionInFlight = false
                }
            }
        }

        if (lastCommandOutput.isNotEmpty()) {
            Spacer(Modifier.height(8.dp))
            Text("Última respuesta del daemon:", color = TextSecondary,
                fontFamily = FontFamily.Monospace, fontSize = 10.sp)
            Text(
                text = lastCommandOutput.take(400),
                color = PhosphorGreen,
                fontFamily = FontFamily.Monospace,
                fontSize = 10.sp,
                modifier = Modifier
                    .fillMaxWidth()
                    .background(ObsidianSoft.copy(alpha = 0.6f), RoundedCornerShape(4.dp))
                    .padding(8.dp)
            )
        }

        Spacer(Modifier.height(12.dp))
        if (!moduleActive) {
            InstallHelp()
        } else {
            Text(
                "✓ El módulo espeja la cadena DSP completa al audio de TODO el " +
                        "sistema (Spotify, YouTube, Tidal) vía omega_daemon. " +
                        "La app también procesa su propia salida local vía DSPBridge.",
                color = TextSecondary,
                fontFamily = FontFamily.Monospace,
                fontSize = 10.sp
            )
        }
    }
}

@Composable
private fun StatusRow(label: String, active: Boolean, activeText: String, inactiveText: String) {
    Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(10.dp)) {
        Text(label, color = Color.White.copy(alpha = 0.5f), fontFamily = FontFamily.Monospace,
            fontSize = 11.sp, modifier = Modifier.width(82.dp))
        Box(Modifier.size(8.dp).clip(CircleShape)
            .background(if (active) PhosphorGreen else CoralWarn))
        Text(if (active) activeText else inactiveText,
            color = if (active) PhosphorGreen else CoralWarn,
            fontFamily = FontFamily.Monospace, fontSize = 12.sp, fontWeight = FontWeight.Bold)
    }
}

@Composable
private fun ActionButton(label: String, enabled: Boolean, onClick: () -> Unit) {
    val containerColor = if (enabled) AuroraCyan.copy(alpha = 0.18f) else ObsidianEdge.copy(alpha = 0.4f)
    val borderColor    = if (enabled) AuroraCyan else ObsidianEdge
    val textColor      = if (enabled) AuroraCyan else TextMuted
    Box(
        modifier = Modifier
            .clip(RoundedCornerShape(8.dp))
            .background(containerColor)
            .border(1.dp, borderColor, RoundedCornerShape(8.dp))
            .clickable(enabled = enabled, onClick = onClick)
            .padding(horizontal = 12.dp, vertical = 8.dp)
    ) {
        Text(label, color = textColor, fontFamily = FontFamily.Monospace,
            fontSize = 11.sp, fontWeight = FontWeight.Bold)
    }
}

@Composable
private fun InstallHelp() {
    Column(
        modifier = Modifier.fillMaxWidth()
            .background(ObsidianSoft.copy(alpha = 0.6f), RoundedCornerShape(8.dp))
            .border(1.dp, AmberSignal.copy(alpha = 0.6f), RoundedCornerShape(8.dp))
            .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp)
    ) {
        Text("MÓDULO NO DETECTADO", color = AmberSignal, fontFamily = FontFamily.Monospace,
            fontSize = 11.sp, fontWeight = FontWeight.Bold)
        Text("Para procesar el audio de TODAS las apps del sistema:",
            color = TextSecondary, fontSize = 11.sp)
        Text(
            "1. Flashea magisk_module/ desde Magisk Manager\n" +
                    "2. Reinicia el dispositivo\n" +
                    "3. Verifica persist.ivanna.magisk_active=1\n" +
                    "   desde Termux: getprop persist.ivanna.magisk_active",
            color = Color.White.copy(alpha = 0.8f), fontFamily = FontFamily.Monospace, fontSize = 10.sp
        )
        Text(
            "⚠ Sin el módulo el socket muestra N/A — esto es normal.\n" +
                    "El DSP local de la app sigue activo vía DSPBridge (AudioEffect sessions).",
            color = TextSecondary, fontSize = 10.sp
        )
    }
}

@Composable
private fun DividerGlow() {
    Spacer(
        Modifier.fillMaxWidth().height(1.dp)
            .background(Brush.horizontalGradient(
                listOf(Color.Transparent, ObsidianEdge, Color.Transparent)
            ))
    )
}
