Text("Motor OPE", style = MaterialTheme.typography.titleMedium)
            Spacer(modifier = Modifier.height(4.dp))
            Text(
                when (omegaMode) {
                    1 -> "DSP + NHO (saturación armónica no lineal)"
                    2 -> "DSP + NHO + Spatial (ITD/ILD, imagen estéreo)"
                    3 -> "DSP + NHO + HRTF binaural (convolución FFT real, usar audífonos)"
                    else -> "Solo DSP (EQ/Comp/Exciter/Widener)"
                },
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Spacer(modifier = Modifier.height(8.dp))
            SingleChoiceSegmentedButtonRow(modifier = Modifier.fillMaxWidth()) {
                listOf("DSP", "+NHO", "+Spatial", "+HRTF").forEachIndexed { index, label ->
                    SegmentedButton(
                        selected = omegaMode == index,
                        onClick = {
                            omegaMode = index
                            onOmegaModeChange(index)
                        },
                        shape = SegmentedButtonDefaults.itemShape(index = index, count = 4)
                    ) {
                        Text(label)
                    }
                }
            }
        }
