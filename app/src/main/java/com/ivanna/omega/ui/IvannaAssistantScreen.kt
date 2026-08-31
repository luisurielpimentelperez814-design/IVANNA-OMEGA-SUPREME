package com.ivanna.omega.ui

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.*
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
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
    val panel by vm.panel.collectAsState()
    val scroll = rememberScrollState()

    Scaffold(
        containerColor = ObsidianVoid,
        topBar = {
            IvannaAssistantTopBar(onBack = onBack)
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .verticalScroll(scroll),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
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

            // ── Controles ─────────────────────────────────────────────────
            MicSection(
                phase       = panel.phase,
                micAvailable = panel.micAvailable,
                onStartListen = { vm.startListening() },
                onStopListen  = { vm.stopListening() },
                onTextSend    = { vm.onTextInput(it) }
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
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                preferences.forEach { pref ->
                    AssistChip(
                        onClick    = {},
                        label      = { Text(pref, style = MaterialTheme.typography.labelSmall) },
                        colors     = AssistChipDefaults.assistChipColors(
                            containerColor = PhosphorGreen.copy(alpha = 0.12f),
                            labelColor     = PhosphorGreen
                        ),
                        border     = AssistChipDefaults.assistChipBorder(
                            enabled      = true,
                            borderColor  = PhosphorGreen.copy(alpha = 0.3f),
                            borderWidth  = 1.dp
                        )
                    )
                }
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Mic + Text input section (FASE 17 — fallbacks)
// ─────────────────────────────────────────────────────────────────────────────

@Composable
private fun MicSection(
    phase        : AssistantPhase,
    micAvailable : Boolean,
    onStartListen: () -> Unit,
    onStopListen : () -> Unit,
    onTextSend   : (String) -> Unit
) {
    val context = LocalContext.current
    val permissionLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { isGranted ->
        if (isGranted) {
            onStartListen()
        }
    }
    
    val handleStartListen = {
        if (ContextCompat.checkSelfPermission(context, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED) {
            onStartListen()
        } else {
            permissionLauncher.launch(Manifest.permission.RECORD_AUDIO)
        }
    }

    var textInput by remember { mutableStateOf("") }
    val keyboard  = LocalSoftwareKeyboardController.current

    Column(horizontalAlignment = Alignment.CenterHorizontally, modifier = Modifier.padding(horizontal = 20.dp)) {

        // Mic button (con fallback visual si no hay micrófono)
        if (!micAvailable) {
            Row(
                modifier          = Modifier
                    .fillMaxWidth()
                    .background(CoralWarn.copy(alpha = 0.10f), RoundedCornerShape(8.dp))
                    .padding(12.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Icon(Icons.Default.MicOff, contentDescription = null, tint = CoralWarn,
                    modifier = Modifier.size(18.dp))
                Spacer(Modifier.width(8.dp))
                Text("Micrófono no disponible. Usa el campo de texto.",
                    color = CoralWarn, style = MaterialTheme.typography.bodySmall)
            }
            Spacer(Modifier.height(12.dp))
        } else {
            val isListening = phase == AssistantPhase.LISTENING
            val micColor    = if (isListening) CoralWarn else AuroraCyan

            FilledTonalIconButton(
                onClick  = { if (isListening) onStopListen() else handleStartListen() },
                modifier = Modifier.size(72.dp),
                colors   = IconButtonDefaults.filledTonalIconButtonColors(
                    containerColor = micColor.copy(alpha = 0.15f)
                )
            ) {
                Icon(
                    imageVector        = if (isListening) Icons.Default.MicOff else Icons.Default.Mic,
                    contentDescription = if (isListening) "Detener" else "Hablar",
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
