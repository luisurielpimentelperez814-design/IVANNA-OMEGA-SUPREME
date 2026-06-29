package com.ivanna.omega

import android.media.AudioManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import com.ivanna.omega.core.OmegaEngine
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.dsp.DSPState
import com.ivanna.omega.dsp.DSPViewModel
import com.ivanna.omega.neuromorphic.PiLstmBridge
import kotlin.math.log10

private val Carbon = Color(0xFF0A0A0A)
private val Surface1 = Color(0xFF111111)
private val Surface2 = Color(0xFF181818)
private val Border1 = Color(0xFF222222)
private val CyanGlow = Color(0xFF00F5FF)
private val CyanDim = Color(0x3300F5FF)
private val GoldGlow = Color(0xFFFFD700)
private val TextPri = Color(0xFFFFFFFF)
private val TextSec = Color(0xFF888888)
private val TextMid = Color(0xFFCCCCCC)

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        val am = getSystemService(AUDIO_SERVICE) as AudioManager
        val sr = am.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE)?.toIntOrNull() ?: 48000
        DSPBridge.init(sr)
        setContent { OmegaApp() }
    }
}

@Composable
fun OmegaApp() {
    val nav = rememberNavController()
    val dspViewModel: DSPViewModel = viewModel()
    val dsp = dspViewModel.state
    MaterialTheme(colorScheme = darkColorScheme(background = Carbon, surface = Surface1)) {
        NavHost(nav, startDestination = "splash") {
            composable("splash") { SplashScreen { nav.navigate("intro") } }
            composable("intro") { IntroScreen { nav.navigate("dashboard") } }
            composable("dashboard") { DashboardScreen(dspViewModel) }
        }
    }
}

@Composable
fun SplashScreen(onAccept: () -> Unit) {
    Box(Modifier.fillMaxSize().background(Carbon).windowInsetsPadding(WindowInsets.systemBars),
        contentAlignment = Alignment.Center) {
        Column(horizontalAlignment = Alignment.CenterHorizontally,
            modifier = Modifier.padding(horizontal = 28.dp)) {
            Text("IVANNA-OMEGA-SUPREME", color = TextPri, fontSize = 22.sp,
                fontWeight = FontWeight.ExtraBold, letterSpacing = 2.sp)
            Spacer(Modifier.height(4.dp))
            Text("GORE TNS · LUPP-OR9 · DSP ENGINE v1.0", color = CyanGlow,
                fontSize = 10.sp, letterSpacing = 1.5.sp)
            Spacer(Modifier.height(6.dp))
            Text("PI-LSTM Milenio · Neuro-Cochlear · Volterra H2 · Ω-Atlas",
                color = GoldGlow, fontSize = 9.sp, letterSpacing = 1.sp,
                textAlign = TextAlign.Center)
            Spacer(Modifier.height(8.dp))
            Box(Modifier.height(1.dp).width(260.dp).background(
                Brush.horizontalGradient(listOf(Color.Transparent, CyanGlow, Color.Transparent))))
            Spacer(Modifier.height(36.dp))
            Column(Modifier.fillMaxWidth().border(1.dp, Border1, RoundedCornerShape(12.dp))
                .background(Surface2, RoundedCornerShape(12.dp)).padding(16.dp)) {
                Text("AVISO LEGAL", color = CyanGlow, fontSize = 13.sp,
                    fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
                Spacer(Modifier.height(10.dp))
                Text("© 2025–2026 Luis Uriel Pimentel Pérez · GORE TNS. " +
                    "Software propietario y confidencial. " +
                    "Uso no autorizado prohibido. " +
                    "Este software modifica el pipeline de audio del sistema. " +
                    "El usuario asume plena responsabilidad.",
                    color = TextMid, fontSize = 12.sp, lineHeight = 18.sp,
                    textAlign = TextAlign.Justify)
            }
            Spacer(Modifier.height(32.dp))
            Button(onClick = onAccept,
                colors = ButtonDefaults.buttonColors(containerColor = Color.Transparent),
                border = BorderStroke(2.dp, CyanGlow),
                shape = RoundedCornerShape(24.dp),
                modifier = Modifier.fillMaxWidth().height(52.dp)) {
                Text("ACEPTAR E INICIAR", color = TextPri,
                    fontWeight = FontWeight.Bold, letterSpacing = 1.5.sp)
            }
        }
    }
}

@Composable
fun IntroScreen(onEnter: () -> Unit) {
    val bands = listOf("Grand Funk Railroad", "Led Zeppelin", "Rush",
        "Budgie", "Edgar Winter", "Steve Miller Band", "Bachman-Turner Overdrive")
    Column(Modifier.fillMaxSize().background(Carbon)
        .windowInsetsPadding(WindowInsets.systemBars).padding(horizontal = 16.dp),
        horizontalAlignment = Alignment.CenterHorizontally) {
        Spacer(Modifier.height(28.dp))
        Text("EXPERIENCIA AUDITIVA", color = TextPri, fontSize = 18.sp,
            fontWeight = FontWeight.ExtraBold, letterSpacing = 1.5.sp)
        Spacer(Modifier.height(4.dp))
        Text("Hard Rock 70s · DSP + HRTF + Neuromorphic", color = TextSec, fontSize = 10.sp)
        Spacer(Modifier.height(16.dp))
        LazyVerticalGrid(columns = GridCells.Fixed(3), modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(8.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            items(bands) { band ->
                Box(Modifier.aspectRatio(16f/9f).shadow(6.dp, RoundedCornerShape(8.dp))
                    .background(Surface2, RoundedCornerShape(8.dp))
                    .border(1.dp, CyanDim, RoundedCornerShape(8.dp)),
                    contentAlignment = Alignment.Center) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Box(Modifier.size(8.dp).clip(CircleShape).background(CyanGlow.copy(alpha = 0.5f)))
                        Spacer(Modifier.height(4.dp))
                        Text(band, color = TextSec, fontSize = 8.sp, textAlign = TextAlign.Center,
                            modifier = Modifier.padding(horizontal = 4.dp), lineHeight = 11.sp)
                    }
                }
            }
        }
        Spacer(Modifier.height(16.dp))
        Button(onClick = onEnter, modifier = Modifier.fillMaxWidth().height(56.dp),
            colors = ButtonDefaults.buttonColors(containerColor = CyanDim),
            border = BorderStroke(2.dp, CyanGlow), shape = RoundedCornerShape(12.dp)) {
            Text("ENTRAR AL MOTOR", color = TextPri, fontSize = 16.sp,
                fontWeight = FontWeight.ExtraBold, letterSpacing = 1.sp)
        }
        Spacer(Modifier.height(16.dp))
    }
}

@Composable
fun DashboardScreen(dspViewModel: DSPViewModel) {
    val dsp = dspViewModel.state
    val eqActive = dsp.low != 0f || dsp.mid != 0f || dsp.high != 0f || dsp.presence != 0f
    val fxActive = dsp.wet > 0.01f
    val lstmReady = PiLstmBridge.isReady

    Column(Modifier.fillMaxSize().background(Carbon).windowInsetsPadding(WindowInsets.systemBars)) {
        if (!DSPBridge.isLoaded) {
            Box(Modifier.fillMaxWidth().background(Color(0xFF330000)).padding(8.dp),
                contentAlignment = Alignment.Center) {
                Text("⚠ libivanna_omega.so no disponible",
                    color = Color(0xFFFF4444), fontSize = 11.sp)
            }
        }
        Row(Modifier.fillMaxWidth().background(Surface2)
            .padding(horizontal = 16.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween) {
            Column {
                Text("IVANNA-OMEGA-SUPREME", color = TextPri, fontWeight = FontWeight.ExtraBold,
                    fontSize = 12.sp, letterSpacing = 1.2.sp)
                Text("GORE TNS · v1.0", color = CyanGlow, fontSize = 9.sp, letterSpacing = 1.sp)
            }
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                StatusDot(DSPBridge.isLoaded, "DSP")
                StatusDot(eqActive, "EQ")
                StatusDot(fxActive, "FX")
                StatusDot(lstmReady, "LSTM")
            }
        }

        LazyColumn(Modifier.fillMaxSize().padding(horizontal = 10.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
            contentPadding = PaddingValues(vertical = 10.dp)) {

            item {
                DspSection("GAIN STAGE") {
                    // Drive usa curva logarítmica: slider → [0..4]
                    val driveSl = remember(dsp.drive) { DSPState.driveToSlider(dsp.drive) }
                    FaderControl("DRIVE", driveSl, "x%.2f".format(dsp.drive)) { newVal ->
                        val newState = dsp.copy(drive = DSPState.sliderToDrive(newVal))
                        dspViewModel.updateState { newState }
                        newState.pushToNative()                   // ← usa newState, no dspViewModel.state
                    }
                    FaderControl("WET", dsp.wet, "Señal proc.") { newVal ->
                        val newState = dsp.copy(wet = newVal)
                        dspViewModel.updateState { newState }
                        newState.pushToNative()
                    }
                    FaderControl("MIX", dsp.mix, "Seca/Húmeda") { newVal ->
                        val newState = dsp.copy(mix = newVal)
                        dspViewModel.updateState { newState }
                        newState.pushToNative()
                    }
                }
            }
            item {
                DspSection("DSP ENGINE α·β·γ") {
                    FaderControl("ALPHA", dsp.alpha, "Compresor") { newVal ->
                        val newState = dsp.copy(alpha = newVal)
                        dspViewModel.updateState { newState }
                        newState.pushToNative()
                    }
                    FaderControl("BETA", dsp.beta, "Ratio") { newVal ->
                        val newState = dsp.copy(beta = newVal)
                        dspViewModel.updateState { newState }
                        newState.pushToNative()
                    }
                    FaderControl("GAMMA", dsp.gamma, "Width") { newVal ->
                        val newState = dsp.copy(gamma = newVal)
                        dspViewModel.updateState { newState }
                        newState.pushToNative()
                    }
                    val freqSl = remember(dsp.freq) {
                        (log10(dsp.freq.toDouble() / 20.0) / log10(1000.0)).toFloat().coerceIn(0f, 1f)
                    }
                    FaderControl("FREQ", freqSl, "${dsp.freq.toInt()}Hz") { newVal ->
                        val newState = dsp.copy(freq = DSPState.sliderToFreq(newVal))
                        dspViewModel.updateState { newState }
                        newState.pushToNative()
                    }
                    val qSl = remember(dsp.resonance) {
                        (log10(dsp.resonance.toDouble() / 0.1) / log10(100.0)).toFloat().coerceIn(0f, 1f)
                    }
                    FaderControl("RES", qSl, "Q=%.2f".format(dsp.resonance)) { newVal ->
                        val newState = dsp.copy(resonance = DSPState.sliderToQ(newVal))
                        dspViewModel.updateState { newState }
                        newState.pushToNative()
                    }
                }
            }
            item {
                DspSection("PARAMETRIC EQ  [±18 dB]") {
                    EqFader("LOW", dsp.low) { newVal ->
                        val newState = dsp.copy(low = newVal)
                        dspViewModel.updateState { newState }
                        newState.pushToNative()
                    }
                    EqFader("MID", dsp.mid) { newVal ->
                        val newState = dsp.copy(mid = newVal)
                        dspViewModel.updateState { newState }
                        newState.pushToNative()
                    }
                    EqFader("HIGH", dsp.high) { newVal ->
                        val newState = dsp.copy(high = newVal)
                        dspViewModel.updateState { newState }
                        newState.pushToNative()
                    }
                    EqFader("PRESENCE", dsp.presence) { newVal ->
                        val newState = dsp.copy(presence = newVal)
                        dspViewModel.updateState { newState }
                        newState.pushToNative()
                    }
                    EqFader("MASTER", dsp.master) { newVal ->
                        val newState = dsp.copy(master = newVal)
                        dspViewModel.updateState { newState }
                        newState.pushToNative()
                    }
                }
            }
            item {
                var mode by remember { mutableStateOf(OmegaEngine.getMode()) }
                Column(Modifier.fillMaxWidth().border(1.dp, Border1, RoundedCornerShape(10.dp))
                    .background(Surface1, RoundedCornerShape(10.dp)).padding(10.dp)) {
                    Text("PROCESSING MODE", color = GoldGlow, fontSize = 10.sp,
                        fontWeight = FontWeight.Bold, letterSpacing = 1.5.sp)
                    Spacer(Modifier.height(8.dp))
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        listOf("DSP", "DSP+LSTM", "FULL").forEachIndexed { idx, label ->
                            val sel = mode == idx
                            OutlinedButton(onClick = { mode = idx; OmegaEngine.setMode(idx) },
                                modifier = Modifier.weight(1f),
                                border = BorderStroke(1.dp, if (sel) CyanGlow else Border1),
                                colors = ButtonDefaults.outlinedButtonColors(
                                    containerColor = if (sel) CyanDim else Color.Transparent)) {
                                Text(label, color = if (sel) CyanGlow else TextSec,
                                    fontSize = 9.sp, fontWeight = FontWeight.Bold)
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
fun StatusDot(active: Boolean, label: String) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Box(Modifier.size(8.dp).clip(CircleShape)
            .background(if (active) CyanGlow else Color(0xFF333333)))
        Text(label, color = if (active) CyanGlow else Color(0xFF444444), fontSize = 7.sp)
    }
}

@Composable
fun DspSection(title: String, content: @Composable RowScope.() -> Unit) {
    Column(Modifier.fillMaxWidth().border(1.dp, Border1, RoundedCornerShape(10.dp))
        .background(Surface1, RoundedCornerShape(10.dp)).padding(10.dp)) {
        Text(title, color = CyanGlow, fontSize = 10.sp,
            fontWeight = FontWeight.Bold, letterSpacing = 1.5.sp)
        Spacer(Modifier.height(8.dp))
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceEvenly,
            content = content)
    }
}

@Composable
fun FaderControl(name: String, value: Float, desc: String, onValueChange: (Float) -> Unit) {
    Column(horizontalAlignment = Alignment.CenterHorizontally, modifier = Modifier.width(54.dp)) {
        Text("%.2f".format(value), color = CyanGlow, fontSize = 9.sp, fontWeight = FontWeight.Bold)
        Spacer(Modifier.height(2.dp))
        Box(Modifier.width(34.dp).height(88.dp), contentAlignment = Alignment.Center) {
            Slider(value = value, onValueChange = onValueChange,
                modifier = Modifier.width(88.dp).rotate(-90f),
                colors = SliderDefaults.colors(thumbColor = CyanGlow,
                    activeTrackColor = CyanGlow, inactiveTrackColor = Border1))
        }
        Spacer(Modifier.height(2.dp))
        Text(name, color = TextPri, fontSize = 10.sp, fontWeight = FontWeight.Bold,
            textAlign = TextAlign.Center)
        Text(desc, color = TextSec, fontSize = 7.sp,
            textAlign = TextAlign.Center, lineHeight = 9.sp)
    }
}

@Composable
fun EqFader(name: String, db: Float, onDbChange: (Float) -> Unit) {
    val sliderVal = DSPState.dbToSlider(db)
    // Visual feedback: cyan normal, gold when "hot" (>+9 dB out of ±18 dB range)
    val thumbColor = when {
        db > 9f -> GoldGlow
        db > 0f -> CyanGlow
        else    -> Color(0xFF00AACC)
    }
    Column(horizontalAlignment = Alignment.CenterHorizontally, modifier = Modifier.width(54.dp)) {
        Text(if (db >= 0) "+%.1f".format(db) else "%.1f".format(db),
            color = thumbColor, fontSize = 9.sp, fontWeight = FontWeight.Bold)
        Spacer(Modifier.height(2.dp))
        Box(Modifier.width(34.dp).height(88.dp), contentAlignment = Alignment.Center) {
            Slider(value = sliderVal, onValueChange = { onDbChange(DSPState.sliderToDb(it)) },
                modifier = Modifier.width(88.dp).rotate(-90f),
                colors = SliderDefaults.colors(
                    thumbColor = thumbColor,
                    activeTrackColor = thumbColor,
                    inactiveTrackColor = Border1))
        }
        Spacer(Modifier.height(2.dp))
        Text(name, color = TextPri, fontSize = 10.sp, fontWeight = FontWeight.Bold,
            textAlign = TextAlign.Center)
        Text("dB", color = TextSec, fontSize = 7.sp, textAlign = TextAlign.Center)
    }
}

