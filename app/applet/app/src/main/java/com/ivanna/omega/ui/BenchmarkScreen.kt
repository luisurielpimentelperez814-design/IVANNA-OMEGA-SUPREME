package com.ivanna.omega.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.ivanna.omega.spatial.BenchmarkRunner
import org.json.JSONObject

@Composable
fun BenchmarkScreen(onBack: () -> Unit) {
    val context = LocalContext.current
    // Guardamos ambos: el String visible en la Card y el JSONObject original
    // para acceder a métricas granulares (dsp_roundtrip_median_us / p99) sin
    // reparsear el String en cada recomposición Compose.
    var benchmarkText by remember { mutableStateOf<String?>(null) }
    var benchmarkJson by remember { mutableStateOf<JSONObject?>(null) }
    
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp)
            .verticalScroll(rememberScrollState()),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        Text("Fase 6: Evidence & Benchmark Layer", style = MaterialTheme.typography.titleLarge)
        
        Button(onClick = {
            val results = BenchmarkRunner.runAutomatedBenchmark(context)
            BenchmarkRunner.generateAbxDataset(context)
            benchmarkJson = results
            benchmarkText = results.toString(4)
        }) {
            Text("Ejecutar Benchmark Automático y Suite Acústica")
        }
        
        if (benchmarkText != null) {
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("Resultados del Benchmark (Telemetría Real)", style = MaterialTheme.typography.titleMedium)
                    Spacer(modifier = Modifier.height(8.dp))
                    Text(benchmarkText ?: "")
                    // FIX(compile 2026-08-14): antes leía `results` fuera del
                    // scope del onClick — Unresolved reference en compileDebugKotlin.
                    // Usamos benchmarkJson (State<JSONObject?>) que sobrevive al
                    // lambda y es el mismo objeto que ya se serializó a texto.
                    benchmarkJson?.let { json ->
                        val m = json.optDouble("dsp_roundtrip_median_us", -1.0)
                        val p = json.optDouble("dsp_roundtrip_p99_us", -1.0)
                        if (m >= 0.0) {
                            Spacer(modifier = Modifier.height(8.dp))
                            Text("DSP round-trip (CLOCK_MONOTONIC, n=100): " +
                                 "mediana ${"%.1f".format(m)} µs · p99 ${"%.1f".format(p)} µs",
                                 color = MaterialTheme.colorScheme.primary)
                        }
                    }
                    Spacer(modifier = Modifier.height(8.dp))
                    Text("Dataset ABX (n=30) generado y exportado a CSV.", color = MaterialTheme.colorScheme.primary)
                }
            }
            
            benchmarkJson?.let { json ->
                Card(modifier = Modifier.fillMaxWidth()) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text("Comparativa (Datos Reales)", style = MaterialTheme.typography.titleMedium)
                        Spacer(modifier = Modifier.height(8.dp))
                        val latMs   = json.optDouble("latency_ms", -1.0)
                        val itdUs   = json.optDouble("itd_error_us", -1.0)
                        val ildDb   = json.optDouble("ild_error_db", -1.0)
                        val freqDb  = json.optDouble("frequency_response_deviation_db", -1.0)
                        val memMb   = json.optLong("memory_mb", -1L)
                        val src     = json.optString("acoustic_metrics_source", "")
                        fun d(v: Double, unit: String, dec: Int = 2) =
                            if (v < 0) "—" else "${"%.${dec}f".format(v)} $unit"
                        Text("Latencia DSP round-trip: ${d(latMs, "ms")}")
                        Text("Error ITD: ${d(itdUs, "µs")}  ·  ILD: ${d(ildDb, "dB")}")
                        Text("Desv. respuesta frecuencial: ${d(freqDb, "dB rms")}")
                        Text("Memoria JVM: ${if (memMb > 0) "$memMb MB" else "—"}")
                        if (src.isNotBlank()) {
                            Spacer(modifier = Modifier.height(4.dp))
                            Text(
                                "Métricas acústicas: estimación de modelo ($src) — no medición directa",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }
                }
            }
        }
        
        Button(onClick = onBack) {
            Text("Volver")
        }
    }
}
