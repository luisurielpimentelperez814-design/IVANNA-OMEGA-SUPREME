// UnifiedEngineControlActivity.kt
// ============================================================================
// IVANNA OMEGA SUPREME — Unified Engine Control UI
// ============================================================================
package com.ivanna.omega.unified

import android.os.Bundle
import android.util.Log
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

private const val TAG = "IVANNA_UNIFIED"

// ============================================================================
// MAIN ACTIVITY
// ============================================================================
class UnifiedEngineControlActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        setContent {
            IvannaUnifiedTheme {
                UnifiedEngineScreen()
            }
        }
    }
}

// ============================================================================
// MAIN UI SCREEN
// ============================================================================
@Composable
fun UnifiedEngineScreen() {
    var engineInitialized by remember { mutableStateOf(false) }
    var engineStatus by remember { mutableStateOf(0) }
    
    // Motor states
    var antiDolbyEnabled by remember { mutableStateOf(false) }
    var spatialEnabled by remember { mutableStateOf(false) }
    var evolutionaryEnabled by remember { mutableStateOf(false) }
    var phaseOracleEnabled by remember { mutableStateOf(false) }
    
    // Motor values
    var eqGain by remember { mutableStateOf(0f) }
    var exciterWet by remember { mutableStateOf(0f) }
    var stereoWidth by remember { mutableStateOf(0.5f) }
    var spatialAngle by remember { mutableStateOf(0f) }
    var spatialWidth by remember { mutableStateOf(0.5f) }
    var compRatio by remember { mutableStateOf(4f) }
    
    // Health monitoring
    var motorHealth by remember { mutableStateOf(BooleanArray(6)) }
    var outputLUFS by remember { mutableStateOf(-23f) }
    var outputPeak by remember { mutableStateOf(-6f) }
    var evolutionaryGen by remember { mutableStateOf(0) }
    
    // YAMNet scores
    var yamnetVoice by remember { mutableStateOf(0f) }
    var yamnetMusic by remember { mutableStateOf(0f) }
    var yamnetBass by remember { mutableStateOf(0f) }
    
    // Initialize engine
    LaunchedEffect(Unit) {
        try {
            engineInitialized = IvannaUnifiedNative.nativeInitialize()
            if (engineInitialized) {
                Log.i(TAG, "Engine initialized successfully")
            } else {
                Log.e(TAG, "Engine initialization failed")
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error initializing engine", e)
        }
    }
    
    // Status update loop
    LaunchedEffect(engineInitialized) {
        if (!engineInitialized) return@LaunchedEffect
        while (true) {
            try {
                engineStatus = IvannaUnifiedNative.nativeGetStatus()
                motorHealth = IvannaUnifiedNative.nativeGetMotorHealth()
                outputLUFS = IvannaUnifiedNative.nativeGetOutputLUFS()
                outputPeak = IvannaUnifiedNative.nativeGetOutputPeak()
                evolutionaryGen = IvannaUnifiedNative.nativeGetEvolutionaryGen()
                
                kotlinx.coroutines.delay(500)
            } catch (e: Exception) {
                Log.e(TAG, "Error updating status", e)
            }
        }
    }
    
    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(Color(0xFF0A0E27))
            .verticalScroll(rememberScrollState())
            .padding(16.dp)
    ) {
        // ── HEADER ────────────────────────────────────────────────────
        HeaderSection(
            engineInitialized = engineInitialized,
            engineStatus = engineStatus,
            motorHealth = motorHealth,
            outputLUFS = outputLUFS,
            outputPeak = outputPeak
        )
        
        Spacer(modifier = Modifier.height(24.dp))
        
        // ── MOTOR 1: YAMNET (Anti-Dolby) ──────────────────────────
        Motor1YAMNetSection(
            enabled = antiDolbyEnabled,
            onEnableChange = { enabled ->
                antiDolbyEnabled = enabled
                IvannaUnifiedNative.nativeEnableAntiDolby(enabled)
            },
            voiceScore = yamnetVoice,
            musicScore = yamnetMusic,
            bassScore = yamnetBass
        )
        
        Spacer(modifier = Modifier.height(16.dp))
        
        // ── MOTOR 2: AUDIO ENGINE (DSP) ───────────────────────────
        Motor2AudioEngineSection(
            eqGain = eqGain,
            exciterWet = exciterWet,
            stereoWidth = stereoWidth,
            compRatio = compRatio,
            onEQChange = { value ->
                eqGain = value
                IvannaUnifiedNative.nativeSetDSPParam("eq_gain", value)
            },
            onExciterChange = { value ->
                exciterWet = value
                IvannaUnifiedNative.nativeSetDSPParam("exciter_wet", value)
            },
            onWidenerChange = { value ->
                stereoWidth = value
                IvannaUnifiedNative.nativeSetDSPParam("widener", value)
            },
            onCompRatioChange = { value ->
                compRatio = value
                IvannaUnifiedNative.nativeSetDSPParam("comp_ratio", value)
            }
        )
        
        Spacer(modifier = Modifier.height(16.dp))
        
        // ── MOTOR 3: SPATIAL ENGINE ──────────────────────────────
        Motor3SpatialSection(
            enabled = spatialEnabled,
            angle = spatialAngle,
            width = spatialWidth,
            onEnableChange = { enabled ->
                spatialEnabled = enabled
                IvannaUnifiedNative.nativeEnableSpatial(enabled)
            },
            onAngleChange = { value ->
                spatialAngle = value
                IvannaUnifiedNative.nativeSetSpatialAngle(value)
            },
            onWidthChange = { value ->
                spatialWidth = value
                IvannaUnifiedNative.nativeSetSpatialWidth(value)
            }
        )
        
        Spacer(modifier = Modifier.height(16.dp))
        
        // ── MOTOR 4: EVOLUTIONARY KERNEL ──────────────────────────
        Motor4EvolutionarySection(
            enabled = evolutionaryEnabled,
            generation = evolutionaryGen,
            onEnableChange = { enabled ->
                evolutionaryEnabled = enabled
                IvannaUnifiedNative.nativeEnableEvolutionary(enabled)
            }
        )
        
        Spacer(modifier = Modifier.height(16.dp))
        
        // ── MOTOR 5: PHASE ORACLE ────────────────────────────────
        Motor5PhaseOracleSection(
            enabled = phaseOracleEnabled,
            onEnableChange = { enabled ->
                phaseOracleEnabled = enabled
                IvannaUnifiedNative.nativeEnablePhaseOracle(enabled)
            }
        )
        
        Spacer(modifier = Modifier.height(16.dp))
        
        // ── MOTOR 6: OMEGA BRIDGE ────────────────────────────────
        Motor6OmegaBridgeSection(
            isConnected = motorHealth.getOrNull(5) ?: false,
            onReconnect = {
                val success = IvannaUnifiedNative.nativeReconnectOmegaDaemon()
                Log.i(TAG, "OmegaBridge reconnect: $success")
            }
        )
        
        Spacer(modifier = Modifier.height(32.dp))
    }
}

// ============================================================================
// UI COMPONENTS
// ============================================================================

@Composable
fun HeaderSection(
    engineInitialized: Boolean,
    engineStatus: Int,
    motorHealth: BooleanArray,
    outputLUFS: Float,
    outputPeak: Float
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(Color(0xFF1A1F3A), RoundedCornerShape(12.dp))
            .padding(16.dp)
    ) {
        Text(
            "IVANNA OMEGA SUPREME",
            fontSize = 24.sp,
            fontWeight = FontWeight.Bold,
            color = Color(0xFF00D9FF)
        )
        
        Text(
            "Unified 6-Motor Engine Control",
            fontSize = 12.sp,
            color = Color(0xFF888888)
        )
        
        Spacer(modifier = Modifier.height(12.dp))
        
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            StatusIndicator(
                label = "Engine",
                active = engineStatus == 2,
                color = if (engineStatus == 2) Color(0xFF00FF00) else Color(0xFFFF0000)
            )
            StatusIndicator(
                label = "LUFS",
                active = outputLUFS > -30,
                value = "%.1f dB".format(outputLUFS)
            )
            StatusIndicator(
                label = "Peak",
                active = outputPeak < 0,
                value = "%.1f dB".format(outputPeak)
            )
        }
        
        Spacer(modifier = Modifier.height(12.dp))
        
        // Motor health indicators
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            listOf("YAMNet", "DSP", "Spatial", "Evo", "Phase", "Omega").forEachIndexed { idx, name ->
                HealthDot(
                    name = name,
                    alive = motorHealth.getOrNull(idx) ?: false
                )
            }
        }
    }
}

@Composable
fun Motor1YAMNetSection(
    enabled: Boolean,
    onEnableChange: (Boolean) -> Unit,
    voiceScore: Float,
    musicScore: Float,
    bassScore: Float
) {
    MotorCard(title = "Motor 1: YAMNet Classifier") {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text("Anti-Dolby", color = Color.White)
            Switch(checked = enabled, onCheckedChange = onEnableChange)
        }
        
        if (enabled) {
            Spacer(modifier = Modifier.height(12.dp))
            ScoreBar("Voice", voiceScore, Color(0xFF00D9FF))
            ScoreBar("Music", musicScore, Color(0xFFFF00FF))
            ScoreBar("Bass", bassScore, Color(0xFFFFAA00))
        }
    }
}

@Composable
fun Motor2AudioEngineSection(
    eqGain: Float,
    exciterWet: Float,
    stereoWidth: Float,
    compRatio: Float,
    onEQChange: (Float) -> Unit,
    onExciterChange: (Float) -> Unit,
    onWidenerChange: (Float) -> Unit,
    onCompRatioChange: (Float) -> Unit
) {
    MotorCard(title = "Motor 2: AudioEngine (DSP)") {
        ParameterSlider(
            label = "EQ Gain",
            value = eqGain,
            min = -18f,
            max = 18f,
            onValueChange = onEQChange,
            suffix = "dB"
        )
        
        ParameterSlider(
            label = "Harmonic Exciter",
            value = exciterWet,
            min = 0f,
            max = 1f,
            onValueChange = onExciterChange
        )
        
        ParameterSlider(
            label = "Stereo Width",
            value = stereoWidth,
            min = 0f,
            max = 1.5f,
            onValueChange = onWidenerChange
        )
        
        ParameterSlider(
            label = "Compressor Ratio",
            value = compRatio,
            min = 1f,
            max = 20f,
            onValueChange = onCompRatioChange,
            suffix = ":1"
        )
    }
}

@Composable
fun Motor3SpatialSection(
    enabled: Boolean,
    angle: Float,
    width: Float,
    onEnableChange: (Boolean) -> Unit,
    onAngleChange: (Float) -> Unit,
    onWidthChange: (Float) -> Unit
) {
    MotorCard(title = "Motor 3: Spatial Engine (HRTF)") {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text("3D Audio", color = Color.White)
            Switch(checked = enabled, onCheckedChange = onEnableChange)
        }
        
        if (enabled) {
            Spacer(modifier = Modifier.height(12.dp))
            ParameterSlider(
                label = "Spatial Angle",
                value = angle,
                min = 0f,
                max = 120f,
                onValueChange = onAngleChange,
                suffix = "°"
            )
            
            ParameterSlider(
                label = "Spatial Width",
                value = width,
                min = 0.5f,
                max = 1.5f,
                onValueChange = onWidthChange
            )
        }
    }
}

@Composable
fun Motor4EvolutionarySection(
    enabled: Boolean,
    generation: Int,
    onEnableChange: (Boolean) -> Unit
) {
    MotorCard(title = "Motor 4: Evolutionary Kernel (GA)") {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column {
                Text("Auto-Optimize", color = Color.White, fontWeight = FontWeight.Bold)
                Text("Generation: $generation", color = Color(0xFF888888), fontSize = 10.sp)
            }
            Switch(checked = enabled, onCheckedChange = onEnableChange)
        }
    }
}

@Composable
fun Motor5PhaseOracleSection(
    enabled: Boolean,
    onEnableChange: (Boolean) -> Unit
) {
    MotorCard(title = "Motor 5: Phase Oracle (Predictor)") {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text("Phase Prediction", color = Color.White)
            Switch(checked = enabled, onCheckedChange = onEnableChange)
        }
    }
}

@Composable
fun Motor6OmegaBridgeSection(
    isConnected: Boolean,
    onReconnect: () -> Unit
) {
    MotorCard(title = "Motor 6: OmegaBridge (Magisk)") {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(
                    if (isConnected) "● Connected" else "○ Disconnected",
                    color = if (isConnected) Color(0xFF00FF00) else Color(0xFFFF0000),
                    fontWeight = FontWeight.Bold
                )
            }
            Button(
                onClick = onReconnect,
                modifier = Modifier.height(32.dp)
            ) {
                Text("Reconnect", fontSize = 10.sp)
            }
        }
    }
}

// ============================================================================
// REUSABLE UI COMPONENTS
// ============================================================================

@Composable
fun MotorCard(
    title: String,
    content: @Composable () -> Unit
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(Color(0xFF1A1F3A), RoundedCornerShape(12.dp))
            .padding(16.dp)
    ) {
        Text(
            title,
            fontSize = 14.sp,
            fontWeight = FontWeight.Bold,
            color = Color(0xFF00D9FF)
        )
        
        Spacer(modifier = Modifier.height(12.dp))
        
        content()
    }
}

@Composable
fun ParameterSlider(
    label: String,
    value: Float,
    min: Float,
    max: Float,
    onValueChange: (Float) -> Unit,
    suffix: String = ""
) {
    Column(modifier = Modifier.fillMaxWidth()) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Text(label, color = Color(0xFFCCCCCC), fontSize = 12.sp)
            Text(
                "%.2f$suffix".format(value),
                color = Color(0xFF00D9FF),
                fontSize = 12.sp,
                fontWeight = FontWeight.Bold
            )
        }
        
        Slider(
            value = value,
            onValueChange = onValueChange,
            valueRange = min..max,
            modifier = Modifier.fillMaxWidth()
        )
    }
}

@Composable
fun ScoreBar(label: String, value: Float, color: Color) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(label, color = Color(0xFFCCCCCC), fontSize = 11.sp, modifier = Modifier.width(50.dp))
        Box(
            modifier = Modifier
                .weight(1f)
                .height(6.dp)
                .background(Color(0xFF333333), RoundedCornerShape(3.dp))
                .padding(end = 4.dp)
        ) {
            Box(
                modifier = Modifier
                    .fillMaxHeight()
                    .fillMaxWidth(value)
                    .background(color, RoundedCornerShape(3.dp))
            )
        }
        Text("%.0f%%".format(value * 100), color = Color(0xFF00D9FF), fontSize = 11.sp)
    }
}

@Composable
fun StatusIndicator(
    label: String,
    active: Boolean,
    color: Color = Color(0xFF00D9FF),
    value: String = ""
) {
    Row(
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(4.dp)
    ) {
        Box(
            modifier = Modifier
                .size(8.dp)
                .background(if (active) color else Color(0xFF444444), RoundedCornerShape(4.dp))
        )
        Column {
            Text(label, fontSize = 10.sp, color = Color(0xFF888888))
            if (value.isNotEmpty()) {
                Text(value, fontSize = 10.sp, color = color, fontWeight = FontWeight.Bold)
            }
        }
    }
}

@Composable
fun HealthDot(name: String, alive: Boolean) {
    Column(
        modifier = Modifier.width(45.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Box(
            modifier = Modifier
                .size(12.dp)
                .background(
                    if (alive) Color(0xFF00FF00) else Color(0xFF555555),
                    RoundedCornerShape(50)
                )
        )
        Text(name, fontSize = 8.sp, color = Color(0xFFAAAAAA))
    }
}

// ============================================================================
// THEME
// ============================================================================

@Composable
fun IvannaUnifiedTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = darkColorScheme(
            primary = Color(0xFF00D9FF),
            secondary = Color(0xFFFF00FF),
            surface = Color(0xFF0A0E27),
            background = Color(0xFF0A0E27)
        )
    ) {
        content()
    }
}
