pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
    plugins {
        // FIX (build rojo 2026-09-05, log real):
        //   "The applied Android Gradle Plugin version (8.5.1) is lower than
        //    the minimum supported 8.5.2. Please update the Android Gradle
        //    Plugin version to at least 8.5.2."
        // Kotlin 2.2.21 (introducido en fbff8ee6) rechaza AGP <= 8.5.1.
        // Elevamos al minimo compatible (8.5.2 mantiene el DSL actual;
        // 8.6.x requiere cambios adicionales fuera del alcance de este fix).
        id("com.android.application") version "8.5.2"
        id("org.jetbrains.kotlin.android") version "2.2.21"
        id("org.jetbrains.kotlin.plugin.compose") version "2.2.21"
    }
}
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}
rootProject.name = "IVANNA-OMEGA-SUPREME"
include(":app")
