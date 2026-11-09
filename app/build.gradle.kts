import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.serialization")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "com.ivanna.omega"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.ivanna.omega"
        minSdk = 28
        targetSdk = 35

        val vp = Properties()
        rootProject.file("version.properties").inputStream().use { stream -> vp.load(stream) }
        versionCode = vp.getProperty("versionCode").toInt()
        versionName = vp.getProperty("versionName")

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        // No incrustar API keys en BuildConfig: cualquier secreto empaquetado en un APK
        // puede extraerse. La clave de Gemini se obtiene en runtime de SecureConfigurationManager.
        ndk {
            abiFilters += listOf("arm64-v8a")
        }

        externalNativeBuild {
            cmake {
                arguments += listOf("-DCMAKE_CXX_STANDARD=26", "-DANDROID_STL=c++_shared")
            }
        }
    }

    signingConfigs {
        create("release") {
            initWith(getByName("debug"))
            val customKeystore = System.getenv("KEYSTORE_FILE") ?: (project.findProperty("KEYSTORE_FILE") as String?)
            if (customKeystore != null && file(customKeystore).exists()) {
                storeFile = file(customKeystore)
                storePassword = System.getenv("KEYSTORE_PASSWORD") ?: (project.findProperty("KEYSTORE_PASSWORD") as String?) ?: "android"
                keyAlias = System.getenv("KEY_ALIAS") ?: (project.findProperty("KEY_ALIAS") as String?) ?: "androiddebugkey"
                keyPassword = System.getenv("KEY_PASSWORD") ?: (project.findProperty("KEY_PASSWORD") as String?) ?: "android"
            } else {
                // Ensure a valid keystore exists so validateSigningRelease never fails in clean environments
                val localKeystore = storeFile ?: file("${System.getProperty("user.home")}/.android/debug.keystore")
                if (!localKeystore.exists()) {
                    localKeystore.parentFile?.mkdirs()
                    try {
                        val keytool = listOf(
                            "keytool", "-genkeypair", "-v",
                            "-keystore", localKeystore.absolutePath,
                            "-storepass", "android",
                            "-alias", "androiddebugkey",
                            "-keypass", "android",
                            "-keyalg", "RSA",
                            "-keysize", "2048",
                            "-validity", "10000",
                            "-dname", "CN=Android Debug,O=Android,C=US"
                        )
                        ProcessBuilder(keytool).start().waitFor()
                    } catch (_: Exception) {}
                }
                storeFile = localKeystore
                storePassword = "android"
                keyAlias = "androiddebugkey"
                keyPassword = "android"
            }
        }
    }

    buildTypes {
        release {
            signingConfig = signingConfigs.getByName("release")
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
        debug {
            isDebuggable = true
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.30.3"
        }
    }

    ndkVersion = "27.2.12479018"

    androidResources {
        noCompress += listOf("tflite")
        // v2.5.31: ANTES se ignoraba "*.sofa" y el APK no llevaba NINGÚN SOFA (HRTF AES69 + 200 HpIR ARI):
        // IvannaAssetProvider extraía una carpeta sofa/ vacía y los paneles decían "no desplegado" sin root.
        // Los SOFA son parte del producto: viajan en el APK (se extraen a filesDir en segundo plano).
    }

    buildFeatures {
        compose = true
        buildConfig = true
    }

    testOptions {
        unitTests.isReturnDefaultValues = true
    }

    packaging {
        jniLibs {
            useLegacyPackaging = false
            pickFirsts += listOf("lib/arm64-v8a/libc++_shared.so")
            excludes += listOf(
                "lib/arm64-v8a/libomega_effect.so",
                "lib/armeabi-v7a/libomega_effect.so",
                "lib/x86/libomega_effect.so",
                "lib/x86_64/libomega_effect.so"
            )
        }
        resources {
            excludes += listOf(
                "META-INF/DEPENDENCIES",
                "META-INF/LICENSE",
                "META-INF/LICENSE.txt",
                "META-INF/license.txt",
                "META-INF/NOTICE",
                "META-INF/NOTICE.txt",
                "META-INF/notice.txt",
                "META-INF/ASL2.0",
                "META-INF/*.kotlin_module"
            )
        }
    }
}

dependencies {
    // ARCore: seguimiento de cámara y profundidad ambiental para capturas espaciales.
    implementation("com.google.ar:core:1.49.0")

    implementation("androidx.core:core-ktx:1.12.0")
    implementation("androidx.appcompat:appcompat:1.6.1")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.7.0")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.7.0")
    implementation("androidx.lifecycle:lifecycle-livedata-ktx:2.7.0")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.7.0")

    implementation("androidx.compose.ui:ui:1.6.0")
    implementation("androidx.webkit:webkit:1.12.1")
    implementation("androidx.compose.ui:ui-tooling-preview:1.6.0")
    implementation("androidx.compose.material3:material3:1.2.0")
    implementation("androidx.compose.material:material-icons-extended:1.7.8")
    implementation("androidx.compose.runtime:runtime-livedata:1.6.0")

    implementation("androidx.activity:activity-compose:1.8.2")
    implementation("androidx.navigation:navigation-compose:2.7.7")

    implementation("com.google.android.material:material:1.11.0")
    implementation("androidx.media:media:1.8.0")

    implementation("androidx.security:security-crypto-ktx:1.1.0")

    implementation("org.tensorflow:tensorflow-lite:2.14.0")
    implementation("org.tensorflow:tensorflow-lite-support:0.4.4")

    implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.11.0")
    implementation("org.jetbrains.kotlinx:kotlinx-serialization-core:1.11.0")
    implementation("com.google.code.gson:gson:2.15.0")

    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.11.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.11.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-play-services:1.11.0")

    implementation(platform("com.google.firebase:firebase-bom:35.1.0"))
    implementation("com.google.firebase:firebase-firestore")
    implementation("com.google.firebase:firebase-auth")
    implementation("com.google.firebase:firebase-appcheck-debug:20.0.0") {
        exclude(group = "org.jetbrains.kotlinx", module = "kotlinx-coroutines-core")
        exclude(group = "org.jetbrains.kotlinx", module = "kotlinx-coroutines-core-jvm")
        exclude(group = "org.jetbrains.kotlinx", module = "kotlinx-coroutines-android")
        exclude(group = "org.jetbrains.kotlinx", module = "kotlinx-serialization-json")
        exclude(group = "org.jetbrains.kotlinx", module = "kotlinx-serialization-json-jvm")
        exclude(group = "org.jetbrains.kotlinx", module = "kotlinx-serialization-core")
        exclude(group = "org.jetbrains.kotlinx", module = "kotlinx-serialization-core-jvm")
    }

    // Firebase Generative AI (Firebase AI Logic en BoM 34.x)
    implementation("com.google.firebase:firebase-ai")

    // Legacy Google AI Client (fallback si Firebase no está disponible)
    implementation("com.google.ai.client.generativeai:generativeai:0.9.0")

    implementation("io.coil-kt:coil-compose:2.7.0")
    implementation("io.coil-kt:coil-video:2.7.0")

    testImplementation("junit:junit:4.13.2")
    // Required by IvannaMemoryArchitectureTest (Robolectric + ApplicationProvider).
    testImplementation("androidx.test:core:1.6.1")
    testImplementation("org.robolectric:robolectric:4.14.1")
}

tasks.register("validateUnifiedVersion") {
    doLast {
        val vp = Properties()
        rootProject.file("version.properties").inputStream().use { stream -> vp.load(stream) }
        val moduleProp = rootProject.file("magisk_module/module.prop").readLines()
        val moduleVersion = moduleProp.firstOrNull { it.startsWith("version=") }?.substringAfter("=")?.removePrefix("v")
        val moduleCode = moduleProp.firstOrNull { it.startsWith("versionCode=") }?.substringAfter("=")
        check(moduleVersion == vp.getProperty("versionName") && moduleCode == vp.getProperty("versionCode")) {
            "❌ DESALINEACIÓN DE VERSIÓN: version.properties=${vp.getProperty("versionName")}(${vp.getProperty("versionCode")}) " +
                "pero module.prop=${moduleVersion}(${moduleCode}). Edita version.properties y sincroniza module.prop."
        }
        logger.lifecycle("✅ Unified Version: ${vp.getProperty("versionName")} (${vp.getProperty("versionCode")}) — APK == módulo")
    }
}
tasks.named("preBuild") { dependsOn("validateUnifiedVersion") }

// ── Seguridad anti-piratería (v2.5.369): genera el pin de certificado ──────
// Lee el SHA-256 del certificado del keystore que firma ESE build y lo
// escribe en app/src/main/cpp/security/ivanna_build_pins.h (generado,
// gitignored). IntegrityGate.kt compara en runtime el cert del APK contra
// ese pin (detecta re-firma). La tarea FALLA si no puede generar el pin:
// un build release sin pin quedaría bloqueado fail-closed. Usa la misma
// lógica de keystore que signingConfig (KEYSTORE_FILE o
// ~/.android/debug.keystore; storepass/alias "android"/"androiddebugkey").
tasks.register<Exec>("generateSecurityPins") {
    val keystoreFile = providers.environmentVariable("KEYSTORE_FILE")
        .orElse(file("${System.getProperty("user.home")}/.android/debug.keystore").absolutePath)
    val storePass = providers.environmentVariable("KEYSTORE_PASSWORD")
        .orElse("android")
    val keyAlias = providers.environmentVariable("KEY_ALIAS")
        .orElse("androiddebugkey")
    commandLine(
        "python3",
        "${projectDir}/../tools/security/generate_build_pins.py",
        "--keystore", keystoreFile.get(),
        "--storepass", storePass.get(),
        "--alias", keyAlias.get(),
        "--out", "${projectDir}/src/main/cpp/security/ivanna_build_pins.h"
    )
}

// El pin de certificado se genera antes de compilar el código nativo
// (ivanna_integrity.cpp lo incluye en build-time).
tasks.named("preBuild") { dependsOn("generateSecurityPins") }

// ── Seguridad anti-piratería fase 2: hash de la .so en runtime ────────────
// Tras externalNativeBuild, hashea libivanna_omega.so y escribe el asset
// app/src/main/assets/ivanna_so_hashes.txt ("libivanna_omega.so <sha256>").
// IntegrityGate.kt (paso 4) compara en runtime el hash de la .so instalada
// contra este asset (detecta sustitución de la .so por una copia modificada).
// La tarea FALLA si la .so no existe (el script sale con error): un APK
// release sin asset quedaría bloqueado fail-closed por IntegrityGate, así
// que es mejor fallar el build que empaquetar sin asset. La CI verifica que
// el APK contenga el asset (ver .github/workflows/build.yml).
androidComponents {
    onVariants { variant ->
        val variantName = variant.name // "debug" / "release" (sin flavors)
        val capName = variantName.replaceFirstChar { it.uppercase() }
        val genTask = tasks.register<Exec>("generateSoHashes$capName") {
            dependsOn("externalNativeBuild$capName")
            // FIX (2026-11-09, falso positivo que bloqueaba TODA instalación
            // release): AGP strippea la .so al empaquetar (quita .symtab), así
            // que la .so del APK tiene OTRO hash que el intermedio de CMake
            // (sin strippear) que se hasheaba antes. IntegrityGate paso 4
            // comparaba el asset contra la .so instalada -> mismatch siempre
            // -> isLocked=true -> DSP/ADE/control loop muertos (RMS 0,
            // clasificador SILENCIO, "ADE no pudo iniciarse" cada 45 s).
            // Verificado en el APK v2.5.369: asset 40c16e7c… vs .so del APK
            // dc5dc3d8… (strippeada, 2.481.456 B). Se hashea la .so
            // STRIPPEADA que realmente se empaqueta (salida de
            // strip<Variant>DebugSymbols); si no existe (debug no strippea),
            // se usa el intermedio de CMake.
            dependsOn(tasks.matching { it.name == "strip${capName}DebugSymbols" })
            val cmakeSoPath = layout.buildDirectory.file(
                "intermediates/cmake/${variantName.lowercase()}/obj/arm64-v8a/libivanna_omega.so"
            )
            val strippedSoPath = layout.buildDirectory.file(
                "intermediates/stripped_native_libs/${variantName.lowercase()}/out/lib/arm64-v8a/libivanna_omega.so"
            )
            val outFile = file("${projectDir}/src/main/assets/ivanna_so_hashes.txt")
            // FIX (2026-11-09, CI run 42583087688): NO declarar la .so
            // strippeada como inputs.file(): Gradle valida que los inputs
            // existan y la .so strippeada no existe hasta que
            // strip<Variant>DebugSymbols corre, así que la validación
            // (WorkValidationException) mataba el build antes de ejecutar.
            // La .so strippeada se resuelve en doFirst (tras el strip, por
            // el dependsOn) y en release el build falla con mensaje claro
            // si no está. El input cmakeSoPath basta para el up-to-date
            // check (la strippeada deriva de ella, determinista).
            inputs.file(cmakeSoPath)
            outputs.file(outFile)
            // La elección se resuelve en ejecución: la .so strippeada existe
            // solo si AGP strippeó (release); en debug se usa el intermedio.
            // En release, si no hay .so strippeada, el build FALLA con
            // mensaje claro en vez de empaquetar un hash que no coincide.
            doFirst {
                val stripped = strippedSoPath.get().asFile
                val cmake = cmakeSoPath.get().asFile
                val so = when {
                    stripped.isFile -> stripped
                    variantName == "release" -> throw GradleException(
                        "generateSoHashes: no se encontró la .so strippeada en ${stripped.absolutePath} " +
                        "(AGP no strippeó o cambió la ruta). Sin ella el asset llevaría el hash del " +
                        "intermedio de CMake y el IntegrityGate bloquearía la instalación release."
                    )
                    else -> cmake
                }
                logger.lifecycle("generateSoHashes: hasheando ${so.absolutePath}")
                commandLine(
                    "python3",
                    "${projectDir}/../tools/security/generate_so_hashes.py",
                    "--so", so.absolutePath,
                    "--out", outFile.absolutePath
                )
            }
        }
        // El asset debe existir antes de empaquetar los assets del APK.
        // matching+configureEach (perezoso): merge<Variant>Assets aún no existe
        // cuando corre onVariants; tasks.named lo resolvería con error y
        // rompería la configuración completa de Gradle (CI 42539043243).
        // La regla se aplica cuando AGP crea la tarea.
        tasks.matching { it.name == "merge${capName}Assets" }.configureEach {
            dependsOn(genTask)
        }
        // Las tareas de Lint leen el directorio de assets (incluido
        // ivanna_so_hashes.txt) sin declarar dependencia: Gradle 8.9 lo
        // rechaza con WorkValidationException (CI 42567801291). Mismo patrón
        // perezoso: las tareas Lint pueden no existir aún en onVariants.
        tasks.matching { it.name == "lintVitalAnalyze${capName}" }.configureEach {
            dependsOn(genTask)
        }
        tasks.matching { it.name == "generate${capName}LintVitalReportModel" }.configureEach {
            dependsOn(genTask)
        }
    }
}

configurations.all {
    resolutionStrategy {
        force(
            "org.jetbrains.kotlin:kotlin-stdlib:2.4.21",
            "org.jetbrains.kotlin:kotlin-stdlib-common:2.4.21",
            "org.jetbrains.kotlinx:kotlinx-coroutines-core:1.11.0",
            "org.jetbrains.kotlinx:kotlinx-coroutines-android:1.11.0",
            "org.jetbrains.kotlinx:kotlinx-coroutines-play-services:1.11.0",
            "org.jetbrains.kotlinx:kotlinx-serialization-core:1.11.0",
            "org.jetbrains.kotlinx:kotlinx-serialization-json:1.11.0"
        )
    }
}

kotlin {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
        // R8 8.8.34 (AGP 8.5.2) no lee metadata Kotlin 2.4 -> warning "error parsing kotlin metadata".
        // Se emite metadata 2.1.0 (solo la version escrita; no cambia lenguaje ni bytecode).
        freeCompilerArgs.add("-Xmetadata-version=2.1.0")
    }
}
