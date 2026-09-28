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
import com.ivanna.omega.spatial.WfsCalibrationManager

/**
 * WfsCalibrationPanel — calibracion geometrica del Wave Field Synthesis.
 * Slider de posicion del oyente (X/Y/Z en la sala 3.5x7.0x3.5 m) y boton de
 * preset 7ch por defecto. Cada cambio -> WfsCalibrationManager -> JNI ->
 * daemon snapshot -> omega_effect -> WfsRenderer (cambio audible real).
 */
@Composable
fun WfsCalibrationPanel(){
    val ctx=LocalContext.current
    var lx by remember{ mutableStateOf(WfsCalibrationManager.listenerX) }
    var ly by remember{ mutableStateOf(WfsCalibrationManager.listenerY) }
    var lz by remember{ mutableStateOf(WfsCalibrationManager.listenerZ) }
    var applied by remember{ mutableStateOf(false) }
    Column(Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement=Arrangement.spacedBy(10.dp)){
        Text("WFS — CALIBRACIÓN DE SALA", style=MaterialTheme.typography.titleMedium)
        Text("Preset: ${WfsCalibrationManager.presetName}")
        Text("Oyente X (ancho) %.2f m".format(lx)); Slider(value=lx, onValueChange={ lx=it; WfsCalibrationManager.setListener(ctx,lx,ly,lz); applied=true }, valueRange=0f..3.5f)
        Text("Oyente Y (altura) %.2f m".format(ly)); Slider(value=ly, onValueChange={ ly=it; WfsCalibrationManager.setListener(ctx,lx,ly,lz); applied=true }, valueRange=0f..3.5f)
        Text("Oyente Z (fondo) %.2f m".format(lz)); Slider(value=lz, onValueChange={ lz=it; WfsCalibrationManager.setListener(ctx,lx,ly,lz); applied=true }, valueRange=0f..7f)
        Row(horizontalArrangement=Arrangement.spacedBy(8.dp)){
            Button(onClick={ WfsCalibrationManager.applyLayout(ctx); applied=true }){ Text("Preset 7ch") }
            Button(onClick={ WfsCalibrationManager.setListener(ctx,1.75f,1.20f,3.50f); lx=1.75f;ly=1.20f;lz=3.50f; applied=true }){ Text("Centro") }
        }
        if(applied) Text("Geometría publicada al DSP (WFS).", style=MaterialTheme.typography.bodySmall)
    }
}
