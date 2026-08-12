package com.ivanna.omega.ui

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.ivanna.omega.core.NativeLibraryLoader
import com.ivanna.omega.spatial.IvannaSpatialNative
import org.json.JSONObject
import java.io.File
import java.util.Date

@Composable
fun AbxTestScreen(onBack: () -> Unit) {
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
                    val file = File("/data/local/tmp/ivanna_abx_results.jsonl") // In production, context.filesDir
                    file.appendText(result.toString() + "\n")
                    
                    testActive = false
                    selectedMatch = null
                },
                enabled = selectedMatch != null
            ) {
                Text("Enviar y Registrar Métricas")
            }
        }
        
        Spacer(modifier = Modifier.height(24.dp))
        Button(onClick = onBack) {
            Text("Volver")
        }
    }
}
