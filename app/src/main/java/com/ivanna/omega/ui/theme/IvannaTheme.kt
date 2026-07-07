package com.ivanna.omega.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp

/**
 * IvannaTheme v3.0 — "Aurora Obsidiana OMNIPOTENTE"
 *
 * REGLA DE ORO: no se borra nada de v2.0. Se conservan los mismos nombres
 * públicos (ObsidianDeep, AuroraCyan, IvannaTheme, etc.) para que el drop-in
 * sobre el proyecto real sea directo — sólo se profundiza la paleta, se añaden
 * tonos de glow/elevación nuevos y se afina la tipografía para lectura de
 * instrumento de precisión bajo el wallpaper OpenGL PBR.
 */

// ── Paleta cromática base (idéntica en nombre a v2.0) ───────────────────────
val ObsidianDeep   = Color(0xFF04050A)
val ObsidianSoft   = Color(0xFF0A101C)
val ObsidianGlass  = Color(0xCC0D1524)
val ObsidianEdge   = Color(0xFF223050)

val AuroraCyan     = Color(0xFF6FF3FF)
val AuroraCyanDim  = Color(0xFF3AB9C6)
val NeonMagenta    = Color(0xFFFF3E86)
val AmberSignal    = Color(0xFFF7B733)
val PhosphorGreen  = Color(0xFF23F09A)
val CoralWarn      = Color(0xFFFF5C4D)

val TextPrimary    = Color(0xFFF1F8FF)
val TextSecondary  = Color(0xFF93A8C6)
val TextMuted      = Color(0xFF57708F)

// ── Nuevos tonos v3.0 (glow / profundidad / cristal premium) ────────────────
val ObsidianVoid     = Color(0xFF010204)  // negro absoluto detrás de todo
val GlassHighlight   = Color(0x33FFFFFF)  // filo superior de cristal (specular)
val GlassShadowInner = Color(0x66000000)  // sombra interna de cristal
val AuroraCyanGlow   = Color(0x556FF3FF)
val NeonMagentaGlow  = Color(0x55FF3E86)
val AmberSignalGlow  = Color(0x55F7B733)
val PhosphorGreenGlow= Color(0x5523F09A)
val OmniGoldCore     = Color(0xFFFFD976)  // núcleo del anillo OMNI (estado supremo)

private val IvannaDarkColors = darkColorScheme(
    primary            = AuroraCyan,
    onPrimary          = ObsidianDeep,
    primaryContainer   = AuroraCyanDim,
    onPrimaryContainer = ObsidianDeep,
    secondary          = NeonMagenta,
    onSecondary        = TextPrimary,
    tertiary           = AmberSignal,
    onTertiary         = ObsidianDeep,
    background         = ObsidianVoid,
    onBackground       = TextPrimary,
    surface            = ObsidianSoft,
    onSurface          = TextPrimary,
    surfaceVariant     = ObsidianGlass,
    onSurfaceVariant   = TextSecondary,
    outline            = ObsidianEdge,
    outlineVariant     = ObsidianEdge,
    error              = CoralWarn,
    onError            = TextPrimary
)

private val Mono = FontFamily.Monospace
private val Sans = FontFamily.SansSerif

val IvannaTypography = Typography(
    displayLarge   = TextStyle(fontFamily = Sans, fontWeight = FontWeight.Light,      fontSize = 46.sp, letterSpacing = 5.sp),
    displayMedium  = TextStyle(fontFamily = Sans, fontWeight = FontWeight.ExtraLight, fontSize = 33.sp, letterSpacing = 4.sp),
    headlineLarge  = TextStyle(fontFamily = Sans, fontWeight = FontWeight.Medium,     fontSize = 25.sp, letterSpacing = 2.sp),
    headlineMedium = TextStyle(fontFamily = Sans, fontWeight = FontWeight.Medium,     fontSize = 20.sp, letterSpacing = 1.sp),
    titleLarge     = TextStyle(fontFamily = Sans, fontWeight = FontWeight.SemiBold,   fontSize = 18.sp, letterSpacing = 0.5.sp),
    titleMedium    = TextStyle(fontFamily = Sans, fontWeight = FontWeight.SemiBold,   fontSize = 15.sp, letterSpacing = 1.3.sp),
    titleSmall     = TextStyle(fontFamily = Sans, fontWeight = FontWeight.Medium,     fontSize = 13.sp, letterSpacing = 1.sp),
    bodyLarge      = TextStyle(fontFamily = Sans, fontWeight = FontWeight.Normal,     fontSize = 15.sp),
    bodyMedium     = TextStyle(fontFamily = Sans, fontWeight = FontWeight.Normal,     fontSize = 13.sp),
    bodySmall      = TextStyle(fontFamily = Sans, fontWeight = FontWeight.Normal,     fontSize = 11.sp, letterSpacing = 0.4.sp),
    labelLarge     = TextStyle(fontFamily = Mono, fontWeight = FontWeight.Medium,     fontSize = 13.sp, letterSpacing = 1.sp),
    labelMedium    = TextStyle(fontFamily = Mono, fontWeight = FontWeight.Medium,     fontSize = 11.sp, letterSpacing = 1.sp),
    labelSmall     = TextStyle(fontFamily = Mono, fontWeight = FontWeight.Normal,     fontSize = 10.sp, letterSpacing = 1.2.sp)
)

@Composable
fun IvannaTheme(
    @Suppress("UNUSED_PARAMETER") darkTheme: Boolean = isSystemInDarkTheme(),
    content: @Composable () -> Unit
) {
    MaterialTheme(
        colorScheme = IvannaDarkColors,
        typography  = IvannaTypography,
        content     = content
    )
}
