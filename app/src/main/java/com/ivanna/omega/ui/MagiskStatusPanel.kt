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
 * Fuentes reales de datos (sin inventar nada):
 *   - MagiskBridge.isModuleActive / moduleVersion / isDaemonRunning
 *     → leen setprop del sistema y `test -S /dev/socket/ivanna_omega`
 *   - OmegaEngineBridge.isConnected / requestTelemetry()
 *     → LocalSocket contra omega_daemon_socket (system-wide)
 *
 * Polling cada 2 s. Si el módulo no está activo, muestra instrucciones
 * de instalación (sin el módulo el audio de TODO el sistema no se procesa;
 * el DSP local de la app sí sigue activo vía DSPBridge).
 */
@Composable
fun MagiskStatusPanel(
    omegaBridge: OmegaEngineBridge,
    modifier: Modifier = Modifier
) {
    // AUDIT FIX (crítico): los valores iniciales NO deben evaluar los
    // getters bloqueantes de MagiskBridge de forma síncrona durante la
    // primera composición (hilo principal) — eso disparaba el mismo
    // problema de exec()/su -c descrito abajo en el instante mismo de
    // ABRIR la pantalla, antes de que el LaunchedEffect llegara a
    // correr en Dispatchers.IO. Arranca con valores neutros/seguros;
    // el LaunchedEffect los puebla con datos reales en el primer tick.
    var moduleActive by remember { mutableStateOf(false) }
    var moduleVersion by remember { mutableStateOf("") }
    var daemonRunning by remember { mutableStateOf(false) }
    var daemonConnected by remember { mutableStateOf(omegaBridge.isConnected) }
    var lastCommandOutput by remember { mutableStateOf("") }
    // AUDIT FIX (crítico, crash/ANR confirmado al presionar STATUS/TELEMETRY/
    // RELOAD): MagiskBridge.getStatus()/reloadParams() hacen `su -c` +
    // lectura bloqueante + waitFor(3000ms) — en un dispositivo sin permiso
    // root pre-concedido, `su` dispara el diálogo de superusuario de Magisk,
    // y esa espera ocurría ANTES en el hilo principal directo desde onClick.
    // Con la UI congelada >5s, Android mata la actividad por ANR — lo que el
    // usuario percibe como que la app truena. requestTelemetry() (LocalSocket)
    // tiene el mismo problema. Se mueven las 3 llamadas a Dispatchers.IO.
    val scope = rememberCoroutineScope()
    var actionInFlight by remember { mutableStateOf(false) }

    LaunchedEffect(Unit) {
        while (true) {
            // AUDIT FIX (crítico, crash/ANR real desde que se ABRE la
            // pantalla, no solo al presionar un botón): isModuleActive/
            // moduleVersion usan getSystemProp() y isDaemonRunning usa
            // exec("test -S ...") — ambos vía exec() interno, que hace
            // `su -c ...` + lectura bloqueante + waitFor(3000ms). Este
            // LaunchedEffect corre en el contexto de composición (hilo
            // principal) y repetía esas 3 llamadas bloqueantes CADA 2
            // SEGUNDOS desde el instante en que se abre MagiskStatusPanel —
            // si el dispositivo no tiene el permiso root pre-concedido,
            // cada tick puede disparar el diálogo de superusuario de Magisk
            // y congelar la UI. Con eso, Android puede matar la actividad
            // por ANR (>5s de hilo principal bloqueado) — indistinguible
            // de un crash para quien lo usa. Se mueven las 3 lecturas a
            // Dispatchers.IO; escribir mutableStateOf desde otro hilo es
            // seguro en Compose (dispara recomposición igual).
            val (active, version, running) = withContext(Dispatchers.IO) {
                Triple(
                    MagiskBridge.isModuleActive,
                    MagiskBridge.moduleVersion,
                    MagiskBridge.isDaemonRunning
                )
            }
            moduleActive    = active
            moduleVersion   = version
            daemonRunning   = running
            daemonConnected = omegaBridge.isConnected
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
        Text(
            "MÓDULO MAGISK",
            color = AuroraCyan,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold,
            fontSize = 14.sp,
            letterSpacing = 2.sp
        )

        StatusRow(
            label = "MÓDULO",
            active = moduleActive,
            activeText = "ACTIVO",
            inactiveText = "NO INSTALADO"
        )
        StatusRow(
            label = "VERSIÓN",
            active = moduleVersion.isNotEmpty() && moduleVersion != "unknown",
            activeText = moduleVersion,
            inactiveText = "—"
        )
        StatusRow(
            label = "DAEMON",
            active = daemonRunning,
            activeText = "CORRIENDO",
            inactiveText = "DETENIDO"
        )

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
        StatusRow(
            label = "SOCKET",
            active = daemonConnected,
            activeText = "CONECTADO",
            inactiveText = "DESCONECTADO"
        )
        Text(
            text = "Socket principal: /dev/socket/ivanna_omega\n" +
                    "Fallback legacy:   /data/pf/pf.sock",
            color = TextSecondary,
            fontFamily = FontFamily.Monospace,
            fontSize = 10.sp
        )

        Spacer(Modifier.height(6.dp))
        DividerGlow()

        Text(
            "ACCIONES",
            color = AuroraCyan,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold,
            fontSize = 13.sp,
            letterSpacing = 2.sp
        )

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            ActionButton("STATUS", MagiskBridge.isDaemonRunning && !actionInFlight) {
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
        }

        if (lastCommandOutput.isNotEmpty()) {
            Spacer(Modifier.height(8.dp))
            Text(
                "Última respuesta del daemon:",
                color = TextSecondary,
                fontFamily = FontFamily.Monospace,
                fontSize = 10.sp
            )
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
private fun StatusRow(
    label: String,
    active: Boolean,
    activeText: String,
    inactiveText: String
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(10.dp)
    ) {
        Text(
            label,
            color = Color.White.copy(alpha = 0.5f),
            fontFamily = FontFamily.Monospace,
            fontSize = 11.sp,
            modifier = Modifier.width(82.dp)
        )
        Box(
            modifier = Modifier
                .size(8.dp)
                .clip(CircleShape)
                .background(if (active) PhosphorGreen else CoralWarn)
        )
        Text(
            if (active) activeText else inactiveText,
            color = if (active) PhosphorGreen else CoralWarn,
            fontFamily = FontFamily.Monospace,
            fontSize = 12.sp,
            fontWeight = FontWeight.Bold
        )
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
            .padding(horizontal = 14.dp, vertical = 8.dp)
    ) {
        Text(
            label,
            color = textColor,
            fontFamily = FontFamily.Monospace,
            fontSize = 11.sp,
            fontWeight = FontWeight.Bold
        )
    }
}

@Composable
private fun InstallHelp() {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(ObsidianSoft.copy(alpha = 0.6f), RoundedCornerShape(8.dp))
            .border(1.dp, AmberSignal.copy(alpha = 0.6f), RoundedCornerShape(8.dp))
            .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp)
    ) {
        Text(
            "MÓDULO NO DETECTADO",
            color = AmberSignal,
            fontFamily = FontFamily.Monospace,
            fontSize = 11.sp,
            fontWeight = FontWeight.Bold
        )
        Text(
            "Para procesar el audio de TODAS las apps del sistema:",
            color = TextSecondary,
            fontSize = 11.sp
        )
        Text(
            "1. Flashea magisk_module/ desde Magisk Manager\n" +
                    "2. Reinicia el dispositivo\n" +
                    "3. Verifica persist.ivanna.magisk_active=1\n" +
                    "   desde Termux: getprop persist.ivanna.magisk_active",
            color = Color.White.copy(alpha = 0.8f),
            fontFamily = FontFamily.Monospace,
            fontSize = 10.sp
        )
        Text(
            "Sin el módulo, la app sigue procesando su propia salida local " +
                    "(BridgePlayer + AudioEffect sessions); el núcleo DSP nativo " +
                    "sigue activo, simplemente no se aplica system-wide.",
            color = TextSecondary,
            fontSize = 10.sp
        )
    }
}

@Composable
private fun DividerGlow() {
    Spacer(
        Modifier
            .fillMaxWidth()
            .height(1.dp)
            .background(
                Brush.horizontalGradient(
                    listOf(Color.Transparent, ObsidianEdge, Color.Transparent)
                )
            )
    )
}
