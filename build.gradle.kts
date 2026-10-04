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
        classpath("com.android.tools:r8:9.1.29")
    }
    configurations.classpath {
        resolutionStrategy {
            force("com.android.tools:r8:9.1.29")
        }
    }
}

plugins {
    id("com.android.application") version "8.5.2" apply false
    id("org.jetbrains.kotlin.android") version "2.4.20" apply false
    id("org.jetbrains.kotlin.plugin.serialization") version "2.4.20" apply false
    id("org.jetbrains.kotlin.plugin.compose") version "2.4.20" apply false
}
