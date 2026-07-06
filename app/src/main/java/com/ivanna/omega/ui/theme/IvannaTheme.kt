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
 * IvannaTheme — Paleta "Aurora Obsidiana"
 *
 * Diseño magistral pensado para dejar visible el wallpaper OpenGL PBR
 * (aurora + nebulosa) que corre detrás como capa 0. Todas las superficies
 * son translúcidas y usan acentos cian eléctrico / magenta neón / oro ámbar
 * sobre negro obsidiana profundo.
 */

// ── Paleta cromática ─────────────────────────────────────────────────────────
val ObsidianDeep   = Color(0xFF05070C) // fondo base (bajo el wallpaper)
val ObsidianSoft   = Color(0xFF0B1220) // superficie elevada
val ObsidianGlass  = Color(0xCC0E1626) // tarjeta glass (con alpha)
val ObsidianEdge   = Color(0xFF23324A) // bordes / outline

val AuroraCyan     = Color(0xFF7DF9FF) // primario: eléctrico
val AuroraCyanDim  = Color(0xFF3EC4CE) // primario apagado
val NeonMagenta    = Color(0xFFFF3C7E) // secundario: magenta neón
val AmberSignal    = Color(0xFFF5B301) // acento oro/ámbar (alertas/estado)
val PhosphorGreen  = Color(0xFF22E58B) // éxito / aurora verde
val CoralWarn      = Color(0xFFFF6B4A) // advertencia/crítico

val TextPrimary    = Color(0xFFEAF6FF) // texto principal
val TextSecondary  = Color(0xFF8CA0BC) // texto secundario
val TextMuted      = Color(0xFF5A6F8A) // texto atenuado

private val IvannaDarkColors = darkColorScheme(
    primary            = AuroraCyan,
    onPrimary          = ObsidianDeep,
    primaryContainer   = AuroraCyanDim,
    onPrimaryContainer = ObsidianDeep,
    secondary          = NeonMagenta,
    onSecondary        = TextPrimary,
    tertiary           = AmberSignal,
    onTertiary         = ObsidianDeep,
    background         = ObsidianDeep,
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

// ── Tipografía magistral ─────────────────────────────────────────────────────
// FontFamily.Monospace para lecturas técnicas (dB, ms, gen, fitness).
// SansSerif con weight variado para jerarquía visual limpia y "de instrumento".
private val Mono = FontFamily.Monospace
private val Sans = FontFamily.SansSerif

val IvannaTypography = Typography(
    displayLarge   = TextStyle(fontFamily = Sans, fontWeight = FontWeight.Light,      fontSize = 44.sp, letterSpacing = 4.sp),
    displayMedium  = TextStyle(fontFamily = Sans, fontWeight = FontWeight.ExtraLight, fontSize = 32.sp, letterSpacing = 3.sp),
    headlineLarge  = TextStyle(fontFamily = Sans, fontWeight = FontWeight.Medium,     fontSize = 24.sp, letterSpacing = 2.sp),
    headlineMedium = TextStyle(fontFamily = Sans, fontWeight = FontWeight.Medium,     fontSize = 20.sp, letterSpacing = 1.sp),
    titleLarge     = TextStyle(fontFamily = Sans, fontWeight = FontWeight.SemiBold,   fontSize = 18.sp, letterSpacing = 0.5.sp),
    titleMedium    = TextStyle(fontFamily = Sans, fontWeight = FontWeight.SemiBold,   fontSize = 15.sp, letterSpacing = 1.2.sp),
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
    // @Suppress: parámetro reservado para futura toggle claro/oscuro.
    @Suppress("UNUSED_PARAMETER") darkTheme: Boolean = isSystemInDarkTheme(),
    content: @Composable () -> Unit
) {
    MaterialTheme(
        colorScheme = IvannaDarkColors,
        typography  = IvannaTypography,
        content     = content
    )
}
