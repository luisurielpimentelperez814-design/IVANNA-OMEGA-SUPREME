package com.ivanna.omega.ui

import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.material3.Slider as Material3Slider
import androidx.compose.material3.SliderColors
import androidx.compose.material3.SliderDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import com.ivanna.omega.safmus.SafmusUniversalKernel

/**
 * Project-wide SAFMUS boundary for Material 3 sliders in the com.ivanna.omega.ui package.
 * Values are finite-checked and clamped before rendering or dispatching to state/DSP.
 * Invalid ranges disable the control instead of forwarding corrupt values.
 */
@Composable
fun Slider(
    value: Float,
    onValueChange: (Float) -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    valueRange: ClosedFloatingPointRange<Float> = 0f..1f,
    steps: Int = 0,
    onValueChangeFinished: (() -> Unit)? = null,
    colors: SliderColors = SliderDefaults.colors(),
) {
    val rangeValid = valueRange.start.isFinite() &&
        valueRange.endInclusive.isFinite() &&
        valueRange.start < valueRange.endInclusive
    val safeRange = if (rangeValid) valueRange else 0f..1f
    val safeValue = SafmusUniversalKernel.sanitizeControlValue(
        value = value,
        min = safeRange.start,
        max = safeRange.endInclusive,
    ) ?: safeRange.start
    val safeSteps = if (rangeValid) steps.coerceAtLeast(0) else 0
    val interactionSource = remember { MutableInteractionSource() }

    Material3Slider(
        value = safeValue,
        onValueChange = { proposed ->
            SafmusUniversalKernel.sanitizeControlValue(
                value = proposed,
                min = safeRange.start,
                max = safeRange.endInclusive,
            )?.let(onValueChange)
        },
        modifier = modifier,
        enabled = enabled && rangeValid,
        valueRange = safeRange,
        steps = safeSteps,
        onValueChangeFinished = onValueChangeFinished,
        colors = colors,
        interactionSource = interactionSource,
    )
}
