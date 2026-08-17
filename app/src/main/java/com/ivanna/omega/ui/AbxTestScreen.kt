package com.ivanna.omega.ui

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.ivanna.omega.core.NativeLibraryLoader
import com.ivanna.omega.spatial.AbxResultStore
import com.ivanna.omega.spatial.AbxStats
import com.ivanna.omega.spatial.IvannaSpatialNative
import org.json.JSONObject
import java.io.File
import java.util.Date

@Composable
fun AbxTestScreen(onBack: () -> Unit) {
    val context = androidx.compose.ui.platform.LocalContext.current
    var totalTrials by remember { mutableStateOf(AbxResultStore.loadAll(context).size) }
    var totalHits by remember { mutableStateOf(AbxResultStore.loadAll(context).count { it.optBoolean("correct") }) }
    var lastExport by remember { mutableStateOf<String?>(null) }
    var testActive by remember { mutableStateOf(false) }
    var currentSample by remember { mutableStateOf("A") } // A, B, X
    var selectedMatch by remember { mutableStateOf<String?>(null) }
    var scoreSpatial by remember { mutableStateOf(5f) }
    var scoreNatural by remember { mutableStateOf(5f) }
    
    // Test scenarios: 0=Bypass, 1=Static HRTF, 2=Dynamic HRTF (Head Tracking)
    val scenarioA = 1
    val scenarioB = 2
    val scenarioX = remember { listOf(1, 2).random() }

    fun setScenario(scenario: Int) {
        // Implement logic to switch DSP engine mode between Bypass, Static HRTF, Dynamic HRTF
    }

    Column(
        modifier = Modifier.fillMaxSize().padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        Text("Prueba ABX - Validación Perceptual Espacial", style = MaterialTheme.typography.titleLarge)
        Spacer(modifier = Modifier.height(32.dp))

        if (!testActive) {
            Button(onClick = { testActive = true; setScenario(scenarioA); currentSample = "A" }) {
                Text("Iniciar Test Ciego")
            }
        } else {
            Row(horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                Button(onClick = { setScenario(scenarioA); currentSample = "A" }, colors = ButtonDefaults.buttonColors(containerColor = if (currentSample == "A") MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.secondary)) {
                    Text("Escuchar A")
                }
                Button(onClick = { setScenario(scenarioB); currentSample = "B" }, colors = ButtonDefaults.buttonColors(containerColor = if (currentSample == "B") MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.secondary)) {
                    Text("Escuchar B")
                }
                Button(onClick = { setScenario(scenarioX); currentSample = "X" }, colors = ButtonDefaults.buttonColors(containerColor = if (currentSample == "X") MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.secondary)) {
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
                onClick = {
                    val correct = (selectedMatch == "A" && scenarioX == scenarioA) || (selectedMatch == "B" && scenarioX == scenarioB)
                    val result = JSONObject().apply {
                        put("timestamp", Date().time)
                        put("protocol", "ABX_Static_vs_DynamicHRTF")
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
                    
                    testActive = false
                    selectedMatch = null
                },
                enabled = selectedMatch != null
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
                    Text("Resultados acumulados", style = MaterialTheme.typography.titleMedium)
                    Text("Trials: $totalTrials · Aciertos: $totalHits " +
                         "(${if (totalTrials>0) (100.0*totalHits/totalTrials).toInt() else 0}%)")
                    Text("p-valor (binomial exacto, H0=0.5): ${"%.4f".format(p)}")
                    Text("IC 95%: [${"%.2f".format(ciLo)}, ${"%.2f".format(ciHi)}]")
                    Text(
                        if (totalTrials >= 10 && p < 0.05) "VEREDICTO: PERCEPTIBLE (p<0.05)"
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
        Button(onClick = onBack) {
            Text("Volver")
        }
    }
}
