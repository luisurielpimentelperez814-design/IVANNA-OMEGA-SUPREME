// ============================================================================
// IVANNA OMEGA SUPREME - Root Build Configuration
// Source of truth for plugin versions.
// Kotlin 2.4 requires R8 9.1.29+ for Kotlin metadata parsing.
// Use a published stable R8 artifact.
// ============================================================================

buildscript {
    repositories {
        google()
        mavenCentral()
    }
    dependencies {
        classpath("com.android.tools:r8:9.1.56")
    }
    configurations.classpath {
        resolutionStrategy {
            force("com.android.tools:r8:9.1.56")
            // NOTA 2026-11-10 (fix CI 386): el compilador Kotlin que resolvía
            // Gradle era 2.0.0 (metadata esperada 2.0.0) y rechazaba las
            // librerías con metadata 2.2/2.3/2.4 (coroutines, serialization,
            // stdlib 2.4.21) en :app:compileReleaseKotlin. Forzar el compilador
            // embebido a 2.4.21, igual que el plugin declarado abajo.
            force("org.jetbrains.kotlin:kotlin-compiler-embeddable:2.4.21")
            force("org.jetbrains.kotlin:kotlin-stdlib:2.4.21")
        }
    }
}

plugins {
    id("com.android.application") version "8.5.2" apply false
    id("org.jetbrains.kotlin.android") version "2.4.21" apply false
    id("org.jetbrains.kotlin.plugin.serialization") version "2.4.21" apply false
    id("org.jetbrains.kotlin.plugin.compose") version "2.4.21" apply false
}
