package com.ivanna.omega.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.audio.AudioProfile
import com.ivanna.omega.audio.ProfileManager

/**
 * ProfileSelector — UI para seleccionar y aplicar presets de audio
 * Muestra perfiles: Steve Miller, RUSH, Budgie, Grand Funk Railroad
 */

@Composable
fun ProfileSelectorScreen(
    profileManager: ProfileManager,
    onProfileSelected: (String) -> Unit,
    modifier: Modifier = Modifier
) {
    val allProfiles = remember { profileManager.getAllProfiles() }
    var selectedProfileId by remember { mutableStateOf(profileManager.getCurrentProfileId()) }
    var showTrainingInfo by remember { mutableStateOf(false) }

    Column(
        modifier = modifier
            .fillMaxSize()
            .padding(16.dp)
            .background(Color(0x1F000000), RoundedCornerShape(12.dp))
    ) {
        // ── Header ──────────────────────────────────────────────────────────
        Text(
            "🎸 IVANNA Audio Profiles",
            fontSize = 24.sp,
            fontWeight = FontWeight.Bold,
            color = Color.White,
            modifier = Modifier.padding(bottom = 8.dp)
        )

        Text(
            "Optimized presets for classic rock bands",
            fontSize = 12.sp,
            color = Color.Gray,
            modifier = Modifier.padding(bottom = 16.dp)
        )

        // ── Training Info Button ────────────────────────────────────────────
        Button(
            onClick = { showTrainingInfo = !showTrainingInfo },
            modifier = Modifier
                .fillMaxWidth()
                .padding(bottom = 12.dp),
            colors = ButtonDefaults.buttonColors(
                containerColor = Color(0xFF2196F3)
            )
        ) {
            Text(
                if (showTrainingInfo) "Hide Training Sequence" else "Show Training Sequence",
                fontSize = 12.sp
            )
        }

        // ── Training Sequence Info ──────────────────────────────────────────
        if (showTrainingInfo) {
            TrainingSequenceCard(profileManager = profileManager)
            Spacer(modifier = Modifier.height(12.dp))
        }

        // ── Profiles List ───────────────────────────────────────────────────
        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            items(allProfiles) { profile ->
                ProfileCard(
                    profile = profile,
                    isSelected = selectedProfileId == profile.id,
                    onSelect = {
                        selectedProfileId = profile.id
                        onProfileSelected(profile.id)
                    }
                )
            }
        }
    }
}

@Composable
fun ProfileCard(
    profile: AudioProfile,
    isSelected: Boolean,
    onSelect: () -> Unit,
    modifier: Modifier = Modifier
) {
    val bgColor = if (isSelected) Color(0xFF4CAF50) else Color(0x2F1F1F1F)
    val borderColor = if (isSelected) Color(0xFF81C784) else Color(0xFF424242)

    Card(
        modifier = modifier
            .fillMaxWidth()
            .clickable { onSelect() }
            .background(bgColor, RoundedCornerShape(8.dp)),
        border = androidx.compose.foundation.BorderStroke(
            2.dp,
            borderColor
        ),
        shape = RoundedCornerShape(8.dp),
        colors = CardDefaults.cardColors(containerColor = bgColor)
    ) {
        Column(
            modifier = Modifier.padding(12.dp)
        ) {
            // ── Profile Name ────────────────────────────────────────────────
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    profile.name,
                    fontSize = 16.sp,
                    fontWeight = FontWeight.Bold,
                    color = Color.White
                )

                if (isSelected) {
                    Text(
                        "✓ Active",
                        fontSize = 12.sp,
                        color = Color(0xFF81C784),
                        fontWeight = FontWeight.Bold
                    )
                }
            }

            Spacer(modifier = Modifier.height(4.dp))

            // ── Description ─────────────────────────────────────────────────
            Text(
                profile.description,
                fontSize = 11.sp,
                color = Color.Gray,
                modifier = Modifier.padding(bottom = 8.dp)
            )

            // ── Parameters Summary ──────────────────────────────────────────
            ParametersSummary(profile)

            Spacer(modifier = Modifier.height(8.dp))

            // ── Tags ────────────────────────────────────────────────────────
            TagsRow(tags = profile.tags)
        }
    }
}

@Composable
fun ParametersSummary(profile: AudioProfile) {
    Column {
        Text(
            "Audio Parameters:",
            fontSize = 10.sp,
            color = Color.LightGray,
            fontWeight = FontWeight.SemiBold,
            modifier = Modifier.padding(bottom = 4.dp)
        )

        val params = listOf(
            "Gain" to "%.2f".format(profile.audioEngine.gain),
            "Exciter" to "%.2f".format(profile.audioEngine.exciterAmount),
            "EQ" to "+%.1f dB".format(profile.audioEngine.eqGain),
            "Width" to "%.2f".format(profile.audioEngine.widthAmount)
        )

        Row(
            modifier = Modifier
                .fillMaxWidth()
                .background(Color(0x1AFFFFFF), RoundedCornerShape(4.dp))
                .padding(8.dp),
            horizontalArrangement = Arrangement.SpaceEvenly
        ) {
            params.forEach { (label, value) ->
                Column(
                    horizontalAlignment = Alignment.CenterHorizontally,
                    modifier = Modifier.weight(1f)
                ) {
                    Text(
                        label,
                        fontSize = 9.sp,
                        color = Color.Gray
                    )
                    Text(
                        value,
                        fontSize = 10.sp,
                        fontWeight = FontWeight.Bold,
                        color = Color.White
                    )
                }
            }
        }
    }
}

@Composable
fun TagsRow(tags: List<String>) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .horizontalScroll(androidx.compose.foundation.rememberScrollState()),
        horizontalArrangement = Arrangement.spacedBy(4.dp)
    ) {
        tags.forEach { tag ->
            Surface(
                modifier = Modifier
                    .wrapContentWidth()
                    .padding(0.dp),
                shape = RoundedCornerShape(12.dp),
                color = Color(0xFF2196F3)
            ) {
                Text(
                    tag,
                    fontSize = 9.sp,
                    color = Color.White,
                    modifier = Modifier.padding(
                        horizontal = 8.dp,
                        vertical = 3.dp
                    )
                )
            }
        }
    }
}

@Composable
fun TrainingSequenceCard(
    profileManager: ProfileManager,
    modifier: Modifier = Modifier
) {
    val trainingSequence = profileManager.getTrainingSequence()
    val trainingNotes = profileManager.getTrainingNotes()

    Card(
        modifier = modifier.fillMaxWidth(),
        shape = RoundedCornerShape(8.dp),
        colors = CardDefaults.cardColors(
            containerColor = Color(0x3FFF9800)
        ),
        border = androidx.compose.foundation.BorderStroke(
            1.dp,
            Color(0xFFFF9800)
        )
    ) {
        Column(
            modifier = Modifier.padding(12.dp)
        ) {
            Text(
                "📚 RECOMMENDED TRAINING SEQUENCE",
                fontSize = 12.sp,
                fontWeight = FontWeight.Bold,
                color = Color.White,
                modifier = Modifier.padding(bottom = 8.dp)
            )

            // Training steps
            trainingSequence.forEachIndexed { index, profile ->
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(vertical = 4.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                        "Day ${index + 1}:",
                        fontSize = 11.sp,
                        fontWeight = FontWeight.Bold,
                        color = Color.White,
                        modifier = Modifier.width(50.dp)
                    )

                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            profile.name,
                            fontSize = 11.sp,
                            color = Color.White,
                            fontWeight = FontWeight.SemiBold
                        )
                        Text(
                            profile.description,
                            fontSize = 9.sp,
                            color = Color.LightGray
                        )
                    }
                }
            }

            if (trainingNotes != null) {
                Spacer(modifier = Modifier.height(8.dp))
                Divider(color = Color(0x5FFFFFFF))
                Spacer(modifier = Modifier.height(8.dp))

                Text(
                    trainingNotes,
                    fontSize = 9.sp,
                    color = Color.White,
                    modifier = Modifier.padding(4.dp)
                )
            }
        }
    }
}

// ── Preview ─────────────────────────────────────────────────────────────────

@Composable
fun PreviewProfileCard() {
    val sampleProfile = AudioProfile(
        id = "preview",
        name = "RUSH",
        description = "Progressive rock technical mastery",
        category = "progressive_rock",
        priority = 1,
        audioEngine = com.ivanna.omega.audio.AudioEngineParams(
            gain = 0.80f,
            exciterAmount = 0.55f,
            eqGain = 3.5f,
            widthAmount = 1.20f,
            bypass = false
        ),
        antiDolby = com.ivanna.omega.audio.AntiDolbyParams(
            speechThreshold = 0.55f,
            bassThreshold = 0.45f,
            eqBoost2k4k = 3.5f,
            exciterLowOnly = false,
            widenerMultiplier = 1.15f
        ),
        neuromorphic = com.ivanna.omega.audio.NeuromorphicParams(
            harmonicGain = 0.75f,
            lateralInhibition = 0.85f,
            ohcCompression = 0.60f,
            masterGainDb = -1.0f,
            cochlearBandwidth = "expanded"
        ),
        route = com.ivanna.omega.audio.RouteParams(
            bassBoostDb = 3.5f,
            dialogBoostDb = 4.0f,
            widenerMult = 1.20f
        ),
        tags = listOf("progressive", "technical", "high_dynamics"),
        recommendedFor = "Complex arrangements, technical bass/drums"
    )

    ProfileCard(
        profile = sampleProfile,
        isSelected = true,
        onSelect = { }
    )
}
