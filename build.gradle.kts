// ============================================================================
// IVANNA OMEGA SUPREME - Root Build Configuration
// Source of truth for plugin versions.
// ============================================================================

buildscript {
    repositories {
        google()
        mavenCentral()
    }
    dependencies {
        classpath("com.android.tools:r8:8.8.34")
    }
    configurations.classpath {
        resolutionStrategy {
            force("com.android.tools:r8:8.8.34")
        }
    }
}

plugins {
    id("com.android.application") version "9.4.1" apply false
    id("org.jetbrains.kotlin.android") version "2.4.20" apply false
    id("org.jetbrains.kotlin.plugin.serialization") version "2.4.20" apply false
    id("org.jetbrains.kotlin.plugin.compose") version "2.4.20" apply false
}
