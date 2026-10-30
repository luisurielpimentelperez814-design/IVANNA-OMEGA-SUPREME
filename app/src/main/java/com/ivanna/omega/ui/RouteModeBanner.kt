package com.ivanna.omega.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.audio.AudioBackendSelector
import com.ivanna.omega.audio.RouteCapabilities

/** Aviso honesto de qué funciones no llegan al audio en el modo actual. No aparece con root + daemon. */
@Composable
fun RouteModeBanner(modifier: Modifier = Modifier) {
    val mode by AudioBackendSelector.mode.collectAsState()
    val limits = RouteCapabilities.limitsFor(mode) ?: return
    Column(
        modifier = modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(14.dp))
            .background(Color(0x33FFB300))
            .padding(14.dp)
    ) {
        Text(limits.first, color = Color(0xFFFFD54F), fontSize = 13.sp, fontWeight = FontWeight.Bold)
        Text(
            "Estas funciones requieren el daemon Magisk y no cambian el audio en este modo:",
            color = Color(0xFFE0E0E0), fontSize = 12.sp, modifier = Modifier.padding(top = 4.dp)
        )
        limits.second.forEach {
            Text("• $it", color = Color(0xFFBDBDBD), fontSize = 12.sp, modifier = Modifier.padding(top = 2.dp))
        }
    }
}
