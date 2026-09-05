plugins {
    id("com.android.application") version "8.5.1" apply false
    id("org.jetbrains.kotlin.android") version "2.2.21" apply false
    id("org.jetbrains.kotlin.plugin.serialization") version "2.2.21" apply false
    // FIX (CI rojo): desde Kotlin 2.0 el compilador de Compose es un plugin
    // Gradle independiente y OBLIGATORIO cuando buildFeatures.compose = true.
    // Su versión va ligada a la de Kotlin (misma 2.2.21).
    id("org.jetbrains.kotlin.plugin.compose") version "2.2.21" apply false
}
