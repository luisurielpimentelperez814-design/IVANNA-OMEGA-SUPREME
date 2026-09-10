pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
    // NOTA (auditoria 2026-09-10, flanco Gradle raiz): se ELIMINA el bloque
    // `plugins {}` que vivia aqui duplicando versiones con build.gradle.kts.
    // Dentro de `pluginManagement {}`, un bloque `plugins {}` aplica plugins
    // AL SCRIPT DE SETTINGS — no fija versiones para los proyectos. Las
    // versiones reales se resuelven desde el `plugins {}` de build.gradle.kts
    // raiz (con `apply false`). Tenerlas en dos sitios ya causo dos
    // divergencias documentadas (AGP 8.5.1 vs 8.5.2). Fuente unica de verdad
    // de versiones de plugins: build.gradle.kts raiz.
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
