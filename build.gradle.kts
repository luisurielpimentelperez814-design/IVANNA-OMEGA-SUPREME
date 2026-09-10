// FIX (build rojo 2026-09-05): AGP 8.5.1 rechazado por Kotlin 2.2.21 con
// diagnostico "lower than the minimum supported 8.5.2". Se eleva AGP a
// 8.5.2 tanto aqui como en settings.gradle.kts para evitar divergencia.
plugins {
    id("com.android.application") version "8.5.2" apply false
    id("org.jetbrains.kotlin.android") version "2.4.10" apply false
    id("org.jetbrains.kotlin.plugin.serialization") version "2.4.10" apply false
    // FIX (CI rojo): desde Kotlin 2.0 el compilador de Compose es un plugin
    // Gradle independiente y OBLIGATORIO cuando buildFeatures.compose = true.
    // Su versión va ligada a la de Kotlin (misma 2.2.21).
    id("org.jetbrains.kotlin.plugin.compose") version "2.4.10" apply false
}
