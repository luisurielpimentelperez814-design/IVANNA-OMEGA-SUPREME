package com.ivanna.omega.ui

import android.Manifest
import android.content.pm.PackageManager
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.*
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.clickable
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.core.content.ContextCompat
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.material3.OutlinedTextField
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.lifecycle.viewmodel.compose.viewModel
import com.ivanna.omega.ui.theme.*
import com.ivanna.omega.ui.viewmodels.AssistantPhase
import com.ivanna.omega.ui.viewmodels.IvannaAssistantViewModel

// ─────────────────────────────────────────────────────────────────────────────
// IvannaAssistantScreen — Pantalla principal de IVANNA (FASE 15)
//
// Flujo visual:
//   StatusOrb (fase) ──► IntelligencePanel ──► AudioPanel ──► MemoryPanel
//                                               ↕
//                                   Transcript (último turno)
//                                               ↕
//                                      MicButton + TextInput
// ─────────────────────────────────────────────────────────────────────────────

@Composable
fun IvannaAssistantScreen(
    onBack: () -> Unit = {},
    vm: IvannaAssistantViewModel = viewModel()
) {
    val panel  by vm.panel.collectAsState()
    val scroll = rememberScrollState()
    val context = LocalContext.current

    // ── Runtime permission para RECORD_AUDIO (Android 6+) ─────────────────
    // Sin este check el SpeechRecognizer devuelve ERROR_INSUFFICIENT_PERMISSIONS
    // silenciosamente y el micrófono nunca funciona.
    var hasAudioPermission by remember {
        mutableStateOf(
            ContextCompat.checkSelfPermission(
                context, Manifest.permission.RECORD_AUDIO
            ) == PackageManager.PERMISSION_GRANTED
        )
    }
    val permissionLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.RequestPermission()
    ) { granted ->
        hasAudioPermission = granted
        // Si el usuario acaba de conceder el permiso, IVANNA lo celebra
        if (granted) vm.onTextInput("hola")
    }

    // Nota: el permiso RECORD_AUDIO en tiempo de ejecución (obligatorio desde
    // Android 6 además de declararlo en el manifiesto) ya se gestiona dentro
    // de MicSection() más abajo, junto al botón que lo dispara.

    Scaffold(
        containerColor = ObsidianVoid,
        topBar = { IvannaAssistantTopBar(onBack = onBack) }
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .verticalScroll(scroll),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {

        // ── API key Gemini (cableado FASE final) ────────────────────────────
        // Campo persistente: escribe al ViewModel -> IvannaGeminiAgent.setApiKey()
        // (SharedPreferences + invalida el modelo). Reactivo: geminiAvailable.
        val panelState by vm.panel.collectAsState()
        OutlinedTextField(
            value = panelState.geminiApiKey,
            onValueChange = { vm.setGeminiApiKey(it) },
            modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 4.dp),
            label = { Text("API Key (Google AI Studio)") },
            placeholder = { Text("Pega tu key para habilitar el asistente") },
            singleLine = true,
            visualTransformation = PasswordVisualTransformation(),
            trailingIcon = {
                Text(
                    if (panelState.geminiAvailable) "● Lista" else "○ Requerida",
                    color = if (panelState.geminiAvailable) Color(0xFF4ADE80) else Color(0xFFF87171),
                    style = MaterialTheme.typography.labelSmall
                )
            }
        )
            Spacer(Modifier.height(16.dp))

            // ── Orb de estado ─────────────────────────────────────────────
            PhaseOrb(phase = panel.phase)
            Spacer(Modifier.height(6.dp))
            PhaseLabel(phase = panel.phase)
            Spacer(Modifier.height(8.dp))
            StatusLine(text = panel.statusLine, phase = panel.phase)

            Spacer(Modifier.height(20.dp))

            // ── Transcript ────────────────────────────────────────────────
            if (panel.lastUserText.isNotEmpty() || panel.lastIvannaText.isNotEmpty()) {
                TranscriptCard(
                    userText   = panel.lastUserText,
                    ivannaText = panel.lastIvannaText
                )
                Spacer(Modifier.height(12.dp))
            }

            // ── Panel de inteligencia ─────────────────────────────────────
            IntelligencePanel(
                detectedIntent = panel.detectedIntent,
                activeAgent    = panel.activeAgent,
                lastAction     = panel.lastAction,
                explanation    = panel.explanation
            )
            Spacer(Modifier.height(10.dp))

            // ── Panel de audio ────────────────────────────────────────────
            AudioPanel(
                scene   = panel.currentScene,
                profile = panel.activeProfile,
                dsp     = panel.dspStatus
            )
            Spacer(Modifier.height(10.dp))

            // ── Panel de memoria ──────────────────────────────────────────
            MemoryPanel(
                context     = panel.sessionContext,
                preferences = panel.learnedPreferences,
                onClear     = { vm.clearMemory() }
            )
            Spacer(Modifier.height(24.dp))

            // ── Conexión Gemini ────────────────────────────────────────────
            GeminiConnectPanel(
                connected = panel.geminiAvailable,
                status    = panel.errorMessage
                             ?: if (panel.geminiAvailable) "✅ IVANNA conectada"
                                else "Sin conexión — ingresa API Key",
                onConnect = { key -> vm.setGeminiApiKey(key) },
                onTest    = { vm.testGeminiConnection() }
            )
            Spacer(Modifier.height(8.dp))

            // ── Controles ─────────────────────────────────────────────────
            MicSection(
                phase            = panel.phase,
                micAvailable     = panel.micAvailable,
                hasAudioPermission = hasAudioPermission,
                onRequestPermission = {
                    permissionLauncher.launch(Manifest.permission.RECORD_AUDIO)
                },
                onStartListen    = { vm.startListening() },
                onStopListen     = { vm.stopListening() },
                onTextSend       = { vm.onTextInput(it) }
            )
            Spacer(Modifier.height(32.dp))
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Top bar
// ─────────────────────────────────────────────────────────────────────────────

@OptIn(ExperimentalMaterial3Api::class)

@Composable
private fun IvannaAssistantTopBar(onBack: () -> Unit) {
    TopAppBar(
        title = {
            Column {
                Text(
                    "IVANNA",
                    color      = AuroraCyan,
                    style      = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold,
                    letterSpacing = 3.sp
                )
                Text(
                    "Conversational Acoustic Intelligence",
                    color = TextMuted,
                    style = MaterialTheme.typography.labelSmall
                )
            }
        },
        navigationIcon = {
            IconButton(onClick = onBack) {
                Icon(Icons.Default.ArrowBack, contentDescription = "Volver", tint = AuroraCyan)
            }
        },
        colors = TopAppBarDefaults.topAppBarColors(
            containerColor = ObsidianSoft
        )
    )
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase Orb — indicador animado de estado
// ─────────────────────────────────────────────────────────────────────────────

@Composable
private fun PhaseOrb(phase: AssistantPhase, size: Dp = 80.dp) {
    val color = phaseColor(phase)
    val animatedColor by animateColorAsState(
        targetValue = color, animationSpec = tween(400), label = "orbColor"
    )

    // Pulso cuando está activo
    val pulsing = phase == AssistantPhase.LISTENING || phase == AssistantPhase.SPEAKING
    val pulseScale by animateFloatAsState(
        targetValue    = if (pulsing) 1.12f else 1f,
        animationSpec  = if (pulsing)
            infiniteRepeatable(tween(700), RepeatMode.Reverse)
        else tween(300),
        label = "pulse"
    )

    Box(
        contentAlignment = Alignment.Center,
        modifier = Modifier
            .size(size)
            .scale(pulseScale)
            .clip(CircleShape)
            .background(
                Brush.radialGradient(
                    listOf(animatedColor.copy(alpha = 0.25f), Color.Transparent)
                )
            )
            .border(2.dp, animatedColor, CircleShape)
    ) {
        Icon(
            imageVector        = phaseIcon(phase),
            contentDescription = phase.name,
            tint               = animatedColor,
            modifier           = Modifier.size(36.dp)
        )
    }
}

@Composable
private fun PhaseLabel(phase: AssistantPhase) {
    val label = when (phase) {
        AssistantPhase.IDLE       -> "LISTO"
        AssistantPhase.LISTENING  -> "ESCUCHANDO"
        AssistantPhase.PROCESSING -> "PENSANDO"
        AssistantPhase.EXECUTING  -> "EJECUTANDO"
        AssistantPhase.SPEAKING   -> "HABLANDO"
        AssistantPhase.ERROR      -> "ERROR"
    }
    Text(
        label,
        color       = phaseColor(phase),
        style       = MaterialTheme.typography.labelLarge,
        fontWeight  = FontWeight.Bold,
        letterSpacing = 3.sp
    )
}

@Composable
private fun StatusLine(text: String, phase: AssistantPhase) {
    Text(
        text         = text,
        color        = if (phase == AssistantPhase.ERROR) CoralWarn else TextSecondary,
        style        = MaterialTheme.typography.bodySmall,
        textAlign    = TextAlign.Center,
        maxLines     = 3,
        overflow     = TextOverflow.Ellipsis,
        modifier     = Modifier.padding(horizontal = 32.dp)
    )
}

// ─────────────────────────────────────────────────────────────────────────────
// Transcript card
// ─────────────────────────────────────────────────────────────────────────────

@Composable
private fun TranscriptCard(userText: String, ivannaText: String) {
    SectionCard(title = "ÚLTIMO TURNO", accent = ObsidianEdge) {
        if (userText.isNotEmpty()) {
            Row(verticalAlignment = Alignment.Top, modifier = Modifier.fillMaxWidth()) {
                Icon(Icons.Default.Person, contentDescription = null,
                    tint = TextMuted, modifier = Modifier.size(14.dp).padding(top = 2.dp))
                Spacer(Modifier.width(6.dp))
                Text(userText, color = TextSecondary,
                    style = MaterialTheme.typography.bodySmall, modifier = Modifier.weight(1f))
            }
            Spacer(Modifier.height(6.dp))
        }
        if (ivannaText.isNotEmpty()) {
            Row(verticalAlignment = Alignment.Top, modifier = Modifier.fillMaxWidth()) {
                Icon(Icons.Default.AutoAwesome, contentDescription = null,
                    tint = AuroraCyan, modifier = Modifier.size(14.dp).padding(top = 2.dp))
                Spacer(Modifier.width(6.dp))
                Text(ivannaText, color = TextPrimary,
                    style = MaterialTheme.typography.bodySmall, modifier = Modifier.weight(1f))
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Intelligence Panel (FASE 12 / 14 — intención, agente, acción, explicación)
// ─────────────────────────────────────────────────────────────────────────────

@Composable
private fun IntelligencePanel(
    detectedIntent : String,
    activeAgent    : String,
    lastAction     : String,
    explanation    : String
) {
    SectionCard(title = "INTELIGENCIA", accent = AuroraCyan) {
        IntelRow(Icons.Default.Psychology,    "Intención",  detectedIntent, AuroraCyan)
        IntelRow(Icons.Default.SmartToy,      "Agente",     activeAgent,    PhosphorGreen)
        IntelRow(Icons.Default.PlayCircle,    "Acción",     lastAction,     AmberSignal)
        IntelRow(Icons.Default.Lightbulb,     "Explicación", explanation,   TextSecondary, multiLine = true)
    }
}

@Composable
private fun IntelRow(
    icon      : ImageVector,
    label     : String,
    value     : String,
    valueColor: Color,
    multiLine : Boolean = false
) {
    Row(
        modifier          = Modifier.fillMaxWidth().padding(vertical = 3.dp),
        verticalAlignment = if (multiLine) Alignment.Top else Alignment.CenterVertically
    ) {
        Icon(icon, contentDescription = label,
            tint = TextMuted, modifier = Modifier.size(14.dp).padding(top = 2.dp))
        Spacer(Modifier.width(6.dp))
        Text(label, color = TextMuted,
            style = MaterialTheme.typography.labelSmall, modifier = Modifier.width(72.dp))
        Text(value, color = valueColor,
            style = MaterialTheme.typography.bodySmall,
            maxLines = if (multiLine) 4 else 1,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.weight(1f))
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Audio Panel (escena DSP / perfil activo / estado)
// ─────────────────────────────────────────────────────────────────────────────

@Composable
private fun AudioPanel(scene: String, profile: String, dsp: String) {
    SectionCard(title = "AUDIO DSP", accent = NeonMagenta) {
        AudioRow(Icons.Default.GraphicEq,     "Escena",  scene,   NeonMagenta)
        AudioRow(Icons.Default.Tune,          "Perfil",  profile, AmberSignal)
        AudioRow(Icons.Default.SettingsInputComponent, "DSP",  dsp,     PhosphorGreen)
    }
}

@Composable
private fun AudioRow(icon: ImageVector, label: String, value: String, accent: Color) {
    Row(
        modifier          = Modifier.fillMaxWidth().padding(vertical = 3.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Icon(icon, contentDescription = label,
            tint = accent.copy(alpha = 0.7f), modifier = Modifier.size(14.dp))
        Spacer(Modifier.width(6.dp))
        Text(label, color = TextMuted,
            style = MaterialTheme.typography.labelSmall, modifier = Modifier.width(52.dp))
        Text(value, color = accent,
            style = MaterialTheme.typography.labelMedium,
            maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f))
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Memory Panel (FASE 13 — contexto y preferencias aprendidas)
// ─────────────────────────────────────────────────────────────────────────────

@Composable
private fun MemoryPanel(
    context     : String,
    preferences : List<String>,
    onClear     : () -> Unit
) {
    SectionCard(
        title  = "MEMORIA",
        accent = PhosphorGreen,
        action = {
            TextButton(onClick = onClear, contentPadding = PaddingValues(0.dp)) {
                Text("BORRAR", color = CoralWarn,
                    style = MaterialTheme.typography.labelSmall, letterSpacing = 1.sp)
            }
        }
    ) {
        Text("Contexto de sesión", color = TextMuted, style = MaterialTheme.typography.labelSmall)
        Spacer(Modifier.height(2.dp))
        Text(context, color = TextSecondary, style = MaterialTheme.typography.bodySmall)

        if (preferences.isNotEmpty()) {
            Spacer(Modifier.height(8.dp))
            Text("Preferencias aprendidas", color = TextMuted, style = MaterialTheme.typography.labelSmall)
            Spacer(Modifier.height(4.dp))
            // Chips de preferencia — Surface puro para evitar variaciones de API
            // en distintas versiones de Material3 (el border de AssistChip cambió
            // entre 1.2.x y 1.3.x). Surface es estable en todas las versiones.
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                preferences.forEach { pref ->
                    Surface(
                        shape  = RoundedCornerShape(50),
                        color  = PhosphorGreen.copy(alpha = 0.10f),
                        border = androidx.compose.foundation.BorderStroke(
                            width = 1.dp,
                            color = PhosphorGreen.copy(alpha = 0.35f)
                        )
                    ) {
                        Text(
                            text     = pref,
                            color    = PhosphorGreen,
                            style    = MaterialTheme.typography.labelSmall,
                            modifier = Modifier.padding(horizontal = 10.dp, vertical = 4.dp)
                        )
                    }
                }
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Mic + Text input section (FASE 17 — fallbacks)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * MicSection — controles de voz y texto del asistente.
 *
 * La permission para RECORD_AUDIO se gestiona en el composable padre
 * (IvannaAssistantScreen) con rememberLauncherForActivityResult, de modo
 * que el ciclo de vida del launcher está correctamente acotado al Screen
 * y no se duplica aquí. MicSection recibe el estado ya resuelto.
 */
@Composable private fun MicSection(
    phase               : AssistantPhase,
    micAvailable        : Boolean,
    hasAudioPermission  : Boolean,
    onRequestPermission : () -> Unit,
    onStartListen       : () -> Unit,
    onStopListen        : () -> Unit,
    onTextSend          : (String) -> Unit
) {
    var textInput by remember { mutableStateOf("") }
    val keyboard  = LocalSoftwareKeyboardController.current

    Column(
        horizontalAlignment = Alignment.CenterHorizontally,
        modifier = Modifier.padding(horizontal = 20.dp)
    ) {
        when {
            // ── Micrófono del dispositivo no disponible (sin STT) ──────────
            !micAvailable -> {
                MicWarningBanner("Micrófono no disponible. Usa el campo de texto.")
                Spacer(Modifier.height(12.dp))
            }
            // ── Permiso RECORD_AUDIO no concedido ──────────────────────────
            !hasAudioPermission -> {
                MicWarningBanner("Sin acceso al micrófono.")
                Spacer(Modifier.height(10.dp))
                OutlinedButton(
                    onClick  = onRequestPermission,
                    border   = androidx.compose.foundation.BorderStroke(1.dp, AuroraCyan),
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Icon(Icons.Default.Mic, contentDescription = null,
                        tint = AuroraCyan, modifier = Modifier.size(18.dp))
                    Spacer(Modifier.width(8.dp))
                    Text("Conceder acceso al micrófono",
                        color = AuroraCyan, style = MaterialTheme.typography.labelMedium)
                }
                Spacer(Modifier.height(12.dp))
            }
            // ── Micrófono disponible y con permiso ─────────────────────────
            else -> {
                val isListening = phase == AssistantPhase.LISTENING
                val micColor    = if (isListening) CoralWarn else AuroraCyan

                FilledTonalIconButton(
                    onClick  = { if (isListening) onStopListen() else onStartListen() },
                    modifier = Modifier.size(72.dp),
                    colors   = IconButtonDefaults.filledTonalIconButtonColors(
                        containerColor = micColor.copy(alpha = 0.15f)
                    )
                ) {
                    Icon(
                        imageVector        = if (isListening) Icons.Default.MicOff else Icons.Default.Mic,
                        contentDescription = if (isListening) "Detener escucha" else "Hablar con IVANNA",
                        tint               = micColor,
                        modifier           = Modifier.size(32.dp)
                    )
                }
                Spacer(Modifier.height(4.dp))
                Text(
                    if (isListening) "Toca para detener" else "Toca para hablar",
                    color = TextMuted, style = MaterialTheme.typography.labelSmall
                )
                Spacer(Modifier.height(16.dp))
            }
        }

        // Campo de texto — accesibilidad y pruebas
        OutlinedTextField(
            value         = textInput,
            onValueChange = { textInput = it },
            modifier      = Modifier.fillMaxWidth(),
            placeholder   = { Text("O escribe aquí…", style = MaterialTheme.typography.bodySmall) },
            singleLine    = true,
            trailingIcon  = {
                if (textInput.isNotEmpty()) {
                    IconButton(onClick = {
                        onTextSend(textInput)
                        textInput = ""
                        keyboard?.hide()
                    }) {
                        Icon(Icons.Default.Send, contentDescription = "Enviar", tint = AuroraCyan)
                    }
                }
            },
            keyboardOptions = KeyboardOptions(imeAction = ImeAction.Send),
            keyboardActions = KeyboardActions(onSend = {
                if (textInput.isNotEmpty()) {
                    onTextSend(textInput)
                    textInput = ""
                    keyboard?.hide()
                }
            }),
            colors = OutlinedTextFieldDefaults.colors(
                focusedBorderColor   = AuroraCyan,
                unfocusedBorderColor = ObsidianEdge,
                focusedTextColor     = TextPrimary,
                unfocusedTextColor   = TextSecondary,
                cursorColor          = AuroraCyan
            )
        )
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// GeminiConnectPanel — conexión a Gemini 2.5 Flash
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Panel de conexión a Gemini 2.5 Flash.
 *
 * Muestra el estado de conexión actual, un campo para ingresar la API Key
 * y los botones Conectar / Probar. La key se persiste en SharedPreferences
 * vía IvannaGeminiAgent.setApiKey(key, context) → sobrevive reinicios.
 *
 * Diseño Aurora Obsidiana: borde amarillo ámbar cuando desconectado,
 * verde fosforescente cuando conectado.
 */
@Composable
private fun GeminiConnectPanel(
    connected: Boolean,
    status   : String,
    onConnect: (String) -> Unit,
    onTest   : () -> Unit
) {
    var keyInput   by remember { mutableStateOf("") }
    var showKey    by remember { mutableStateOf(false) }
    var expanded   by remember { mutableStateOf(!connected) }

    val accentColor = if (connected) PhosphorGreen else AmberSignal

    // Pulso animado del indicador de estado
    val infiniteAnim = rememberInfiniteTransition(label = "geminiPulse")
    val pulse by infiniteAnim.animateFloat(
        initialValue   = 0.55f,
        targetValue    = 1.0f,
        animationSpec  = infiniteRepeatable(
            animation = tween(900, easing = FastOutSlowInEasing),
            repeatMode = RepeatMode.Reverse
        ),
        label = "pulse"
    )

    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp),
        color  = ObsidianSoft,
        shape  = RoundedCornerShape(12.dp),
        border = androidx.compose.foundation.BorderStroke(1.dp, accentColor.copy(alpha = 0.40f))
    ) {
        Column(modifier = Modifier.padding(14.dp)) {

            // ── Header: estado + toggle ────────────────────────────────────
            Row(
                modifier          = Modifier
                    .fillMaxWidth()
                    .clickable { expanded = !expanded },
                verticalAlignment = Alignment.CenterVertically
            ) {
                // Indicador luminoso de conexión
                Box(
                    modifier = Modifier
                        .size(10.dp)
                        .alpha(if (connected) 1f else pulse)
                        .background(accentColor, CircleShape)
                )
                Spacer(Modifier.width(10.dp))

                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text       = "IVANNA · INTELIGENCIA EXTENDIDA",
                        color      = accentColor,
                        style      = MaterialTheme.typography.labelMedium,
                        fontWeight = FontWeight.Bold,
                        letterSpacing = 2.sp
                    )
                    Text(
                        text  = status,
                        color = if (connected) PhosphorGreen else TextSecondary,
                        style = MaterialTheme.typography.labelSmall
                    )
                }

                // Botón de prueba (solo cuando conectado y expandido)
                if (connected) {
                    OutlinedButton(
                        onClick = onTest,
                        border  = androidx.compose.foundation.BorderStroke(1.dp, PhosphorGreen.copy(alpha = 0.5f)),
                        contentPadding = PaddingValues(horizontal = 10.dp, vertical = 4.dp),
                        modifier = Modifier.height(30.dp)
                    ) {
                        Text("Probar", color = PhosphorGreen,
                            style = MaterialTheme.typography.labelSmall)
                    }
                    Spacer(Modifier.width(8.dp))
                }

                Icon(
                    imageVector = if (expanded) Icons.Default.ExpandLess else Icons.Default.ExpandMore,
                    contentDescription = null,
                    tint = TextMuted,
                    modifier = Modifier.size(18.dp)
                )
            }

            // ── Cuerpo expandible: campo de key + botón conectar ──────────
            if (expanded) {
                Spacer(Modifier.height(12.dp))

                Text(
                    text  = "Ingresa tu Google AI Studio API Key para activar la " +
                            "inteligencia extendida de IVANNA. Sin key, IVANNA opera en " +
                            "modo offline con inteligencia on-device completa.",
                    color = TextMuted,
                    style = MaterialTheme.typography.bodySmall
                )
                Spacer(Modifier.height(10.dp))

                OutlinedTextField(
                    value         = keyInput,
                    onValueChange = { keyInput = it },
                    modifier      = Modifier.fillMaxWidth(),
                    placeholder   = {
                        Text("AIzaSy… o AQ.…",
                            style = MaterialTheme.typography.bodySmall,
                            color = TextMuted)
                    },
                    singleLine            = true,
                    visualTransformation  = if (showKey) VisualTransformation.None
                                           else PasswordVisualTransformation(),
                    keyboardOptions       = KeyboardOptions(
                        keyboardType = KeyboardType.Password,
                        imeAction    = ImeAction.Done
                    ),
                    keyboardActions = KeyboardActions(onDone = {
                        if (keyInput.isNotBlank()) {
                            onConnect(keyInput)
                            expanded = false
                        }
                    }),
                    trailingIcon = {
                        IconButton(onClick = { showKey = !showKey }) {
                            Icon(
                                imageVector = if (showKey) Icons.Default.VisibilityOff
                                              else Icons.Default.Visibility,
                                contentDescription = if (showKey) "Ocultar key" else "Mostrar key",
                                tint = TextMuted,
                                modifier = Modifier.size(18.dp)
                            )
                        }
                    },
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedBorderColor   = accentColor,
                        unfocusedBorderColor = ObsidianEdge,
                        focusedTextColor     = TextPrimary,
                        unfocusedTextColor   = TextSecondary,
                        cursorColor          = accentColor
                    )
                )
                Spacer(Modifier.height(10.dp))

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    // Botón principal: Conectar
                    Button(
                        onClick  = {
                            if (keyInput.isNotBlank()) {
                                onConnect(keyInput)
                                expanded = false
                            }
                        },
                        enabled  = keyInput.isNotBlank(),
                        modifier = Modifier.weight(1f),
                        colors   = ButtonDefaults.buttonColors(
                            containerColor = accentColor.copy(alpha = 0.20f),
                            contentColor   = accentColor
                        ),
                        border = androidx.compose.foundation.BorderStroke(
                            1.dp, accentColor.copy(alpha = 0.60f)
                        )
                    ) {
                        Icon(Icons.Default.Link, contentDescription = null,
                            modifier = Modifier.size(16.dp))
                        Spacer(Modifier.width(6.dp))
                        Text(
                            text  = if (connected) "Actualizar key" else "Conectar IVANNA",
                            style = MaterialTheme.typography.labelMedium,
                            fontWeight = FontWeight.Bold
                        )
                    }

                    // Botón: Probar conexión actual
                    if (connected) {
                        OutlinedButton(
                            onClick = onTest,
                            border  = androidx.compose.foundation.BorderStroke(
                                1.dp, PhosphorGreen.copy(alpha = 0.5f)
                            )
                        ) {
                            Text("Probar", color = PhosphorGreen,
                                style = MaterialTheme.typography.labelMedium)
                        }
                    }
                }

                Spacer(Modifier.height(4.dp))
                Text(
                    text  = "Obtén tu key gratis en aistudio.google.com",
                    color = TextMuted.copy(alpha = 0.7f),
                    style = MaterialTheme.typography.labelSmall
                )
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ── MicWarningBanner ─────────────────────────────────────────────────────────

@Composable
private fun MicWarningBanner(message: String) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(CoralWarn.copy(alpha = 0.10f), RoundedCornerShape(8.dp))
            .padding(horizontal = 14.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Icon(Icons.Default.MicOff, contentDescription = null,
            tint = CoralWarn, modifier = Modifier.size(18.dp))
        Spacer(Modifier.width(8.dp))
        Text(message, color = CoralWarn, style = MaterialTheme.typography.bodySmall)
    }
}

// Utilidades compartidas
// ─────────────────────────────────────────────────────────────────────────────

@Composable
private fun SectionCard(
    title  : String,
    accent : Color,
    action : (@Composable () -> Unit)? = null,
    content: @Composable ColumnScope.() -> Unit
) {
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp),
        color  = ObsidianSoft,
        shape  = RoundedCornerShape(12.dp),
        border = androidx.compose.foundation.BorderStroke(1.dp, accent.copy(alpha = 0.25f))
    ) {
        Column(modifier = Modifier.padding(14.dp)) {
            Row(
                modifier          = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Box(Modifier.size(3.dp, 16.dp).background(accent, RoundedCornerShape(2.dp)))
                Spacer(Modifier.width(8.dp))
                Text(
                    title,
                    color      = accent,
                    style      = MaterialTheme.typography.labelMedium,
                    fontWeight = FontWeight.Bold,
                    letterSpacing = 2.sp,
                    modifier   = Modifier.weight(1f)
                )
                action?.invoke()
            }
            Spacer(Modifier.height(10.dp))
            content()
        }
    }
}

private fun phaseColor(phase: AssistantPhase): Color = when (phase) {
    AssistantPhase.IDLE       -> TextMuted
    AssistantPhase.LISTENING  -> AuroraCyan
    AssistantPhase.PROCESSING -> AmberSignal
    AssistantPhase.EXECUTING  -> PhosphorGreen
    AssistantPhase.SPEAKING   -> NeonMagenta
    AssistantPhase.ERROR      -> CoralWarn
}

private fun phaseIcon(phase: AssistantPhase): ImageVector = when (phase) {
    AssistantPhase.IDLE       -> Icons.Default.RadioButtonUnchecked
    AssistantPhase.LISTENING  -> Icons.Default.Mic
    AssistantPhase.PROCESSING -> Icons.Default.Psychology
    AssistantPhase.EXECUTING  -> Icons.Default.PlayCircle
    AssistantPhase.SPEAKING   -> Icons.Default.VolumeUp
    AssistantPhase.ERROR      -> Icons.Default.ErrorOutline
}
