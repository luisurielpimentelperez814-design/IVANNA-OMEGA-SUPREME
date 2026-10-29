package com.ivanna.omega.ui

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.ivanna.omega.spatial.AbxTrialSession
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.spatial.AbxResultStore
import com.ivanna.omega.spatial.AbxStats
import com.ivanna.omega.spatial.IvannaSpatialManager
import org.json.JSONObject
import java.util.Date

@Composable
fun AbxTestScreen(onBack: () -> Unit) {
    val context = androidx.compose.ui.platform.LocalContext.current
    var totalTrials by remember { mutableStateOf(AbxResultStore.validTrials(context).size) }
    var totalHits by remember { mutableStateOf(AbxResultStore.validTrials(context).count { it.optBoolean("correct") }) }
    var lastExport by remember { mutableStateOf<String?>(null) }
    var testActive by remember { mutableStateOf(false) }
    var currentSample by remember { mutableStateOf("A") } // A, B, X
    var selectedMatch by remember { mutableStateOf<String?>(null) }
    var scoreSpatial by remember { mutableStateOf(5f) }
    var scoreNatural by remember { mutableStateOf(5f) }
    
    val scenarioA = 1
    val scenarioB = 2
    val session = remember { AbxTrialSession() }
    val scope = rememberCoroutineScope()
    var originalTracking by remember { mutableStateOf<Boolean?>(null) }
    var trialSubject by remember { mutableStateOf("") }
    var busy by remember { mutableStateOf(false) }
    var readyToSubmit by remember { mutableStateOf(false) }
    var status by remember { mutableStateOf("Requiere HRTF cargado y seguimiento de cabeza disponible.") }

    fun restore() {
        originalTracking?.let { enabled ->
            if (enabled) IvannaSpatialManager.resumeHeadTracking()
            else IvannaSpatialManager.pauseHeadTracking()
        }
        originalTracking = null
    }
    DisposableEffect(Unit) { onDispose { restore(); session.cancel() } }

    fun listen(sample: String) {
        if (busy) return
        busy = true
        readyToSubmit = false
        scope.launch {
            try {
                check(IvannaNativeLib.isLoaded && IvannaSpatialManager.isHrtfConfirmed() &&
                    IvannaSpatialManager.activeSubject == trialSubject)
                val dynamic = session.scenario(sample) == 2
                if (dynamic) IvannaSpatialManager.resumeHeadTracking()
                else IvannaSpatialManager.pauseHeadTracking()
                // Registration alone is not evidence that a fresh sensor sample reached JNI.
                if (dynamic) {
                    repeat(20) {
                        if (!IvannaSpatialManager.isHeadTrackingFresh()) delay(50)
                    }
                }
                val accepted = IvannaSpatialManager.isHrtfConfirmed() &&
                    if (dynamic) IvannaSpatialManager.isHeadTrackingFresh()
                    else !IvannaSpatialManager.isHeadTrackingRegistered()
                session.confirm(sample, accepted)
                check(accepted)
                currentSample = sample
                readyToSubmit = session.canSubmit("A")
                status = "Muestra $sample preparada. Escucha antes de responder."
            } catch (_: Exception) {
                session.cancel(); restore(); testActive = false
                status = "Ensayo cancelado: no se confirmó el renderer/sensor. No se registraron resultados."
            } finally { busy = false }
        }
    }


    Column(
        modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        Text("Prueba ABX - Validación Perceptual Espacial", style = MaterialTheme.typography.titleLarge)
        Text(status, style = MaterialTheme.typography.bodySmall)
        Spacer(modifier = Modifier.height(32.dp))

        if (!testActive) {
            Button(enabled = !busy, onClick = {
                if (IvannaSpatialManager.isHrtfConfirmed()) {
                    originalTracking = IvannaSpatialManager.isHeadTrackingRegistered()
                    trialSubject = IvannaSpatialManager.activeSubject
                    session.begin(); testActive = true; selectedMatch = null
                    scoreSpatial = 5f; scoreNatural = 5f
                    listen("A")
                } else status = "Carga un HRTF en el motor espacial antes de iniciar."
            }) {
                Text("Iniciar Test Ciego")
            }
        } else {
            Row(horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                Button(enabled = !busy, onClick = { listen("A") }, colors = ButtonDefaults.buttonColors(containerColor = if (currentSample == "A") MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.secondary)) {
                    Text("Escuchar A")
                }
                Button(enabled = !busy, onClick = { listen("B") }, colors = ButtonDefaults.buttonColors(containerColor = if (currentSample == "B") MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.secondary)) {
                    Text("Escuchar B")
                }
                Button(enabled = !busy, onClick = { listen("X") }, colors = ButtonDefaults.buttonColors(containerColor = if (currentSample == "X") MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.secondary)) {
                    Text("Escuchar X")
                }
            }

            Spacer(modifier = Modifier.height(32.dp))
            Text("X es igual a:")
            Row(horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                Button(onClick = { selectedMatch = "A" }, colors = ButtonDefaults.buttonColors(containerColor = if (selectedMatch == "A") MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.secondary)) { Text("Muestra A") }
                Button(onClick = { selectedMatch = "B" }, colors = ButtonDefaults.buttonColors(containerColor = if (selectedMatch == "B") MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.secondary)) { Text("Muestra B") }
            }

            Spacer(modifier = Modifier.height(24.dp))
            Text("Localización y Externalización (1-10): ${scoreSpatial.toInt()}")
            Slider(value = scoreSpatial, onValueChange = { scoreSpatial = it }, valueRange = 1f..10f, steps = 8)
            
            Text("Naturalidad y Fatiga (1-10): ${scoreNatural.toInt()}")
            Slider(value = scoreNatural, onValueChange = { scoreNatural = it }, valueRange = 1f..10f, steps = 8)

            Spacer(modifier = Modifier.height(32.dp))
            Button(
                onClick = submit@{
                    val dynamic = session.scenario(currentSample) == 2
                    val stillReady = IvannaSpatialManager.isHrtfConfirmed() &&
                        IvannaSpatialManager.activeSubject == trialSubject &&
                        if (dynamic) IvannaSpatialManager.isHeadTrackingFresh()
                        else !IvannaSpatialManager.isHeadTrackingRegistered()
                    if (!stillReady) {
                        session.cancel(); restore(); testActive = false; readyToSubmit = false
                        status = "Ensayo cancelado: cambió el renderer o el sensor antes de registrar."
                        return@submit
                    }
                    val scenarioX = session.scenarioX
                    val correct = session.finish(requireNotNull(selectedMatch))
                    val result = JSONObject().apply {
                        put("timestamp", Date().time)
                        put("protocol", "ABX_Static_vs_DynamicHRTF")
                        put("protocolVersion", 2)
                        put("valid", true)
                        put("hrtfSubject", IvannaSpatialManager.activeSubject)
                        put("verification", "renderer-loaded-and-tracker-state; acoustic-path-not-instrumented")
                        put("scenarioA", scenarioA)
                        put("scenarioB", scenarioB)
                        put("scenarioX", scenarioX)
                        put("userSelection", selectedMatch)
                        put("correct", correct)
                        put("scoreSpatial", scoreSpatial)
                        put("scoreNatural", scoreNatural)
                    }
                    AbxResultStore.record(context, result)
                    totalTrials += 1
                    if (correct) totalHits += 1
                    
                    restore()
                    testActive = false
                    readyToSubmit = false
                    selectedMatch = null
                },
                enabled = !busy && readyToSubmit && session.canSubmit(selectedMatch)
            ) {
                Text("Enviar y Registrar Métricas")
            }
        }
        
        Spacer(modifier = Modifier.height(24.dp))
        if (totalTrials > 0) {
            val p = AbxStats.binomialTest(totalHits, totalTrials)
            val (ciLo, ciHi) = AbxStats.wilsonInterval95(totalHits, totalTrials)
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("Ensayos válidos del protocolo v2", style = MaterialTheme.typography.titleMedium)
                    Text("Trials: $totalTrials · Aciertos: $totalHits " +
                         "(${if (totalTrials>0) (100.0*totalHits/totalTrials).toInt() else 0}%)")
                    Text("p-valor (binomial exacto, H0=0.5): ${"%.4f".format(p)}")
                    Text("IC 95%: [${"%.2f".format(ciLo)}, ${"%.2f".format(ciHi)}]")
                    Text(
                        if (totalTrials >= 10 && p < 0.05) "Diferenciación reportada (p<0.05); no certifica calidad ni la ruta acústica"
                        else "VEREDICTO: NO DEMOSTRADO",
                        color = if (totalTrials >= 10 && p < 0.05)
                            MaterialTheme.colorScheme.primary
                        else MaterialTheme.colorScheme.error
                    )
                    Spacer(modifier = Modifier.height(8.dp))
                    Button(onClick = {
                        lastExport = AbxResultStore.exportJson(context)?.absolutePath
                    }) { Text("Exportar JSON") }
                    lastExport?.let { Text("→ $it", style = MaterialTheme.typography.bodySmall) }
                }
            }
        }
        Button(onClick = { restore(); session.cancel(); onBack() }) {
            Text("Volver")
        }
    }
}
