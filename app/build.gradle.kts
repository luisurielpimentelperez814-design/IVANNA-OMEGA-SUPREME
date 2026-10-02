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

        val geminiKey = (findProperty("GEMINI_API_KEY") as String?)
            ?: System.getenv("GEMINI_API_KEY")
            ?: ""
        buildConfigField("String", "GEMINI_API_KEY", "\"$geminiKey\"")

        ndk {
            abiFilters += listOf("arm64-v8a")
        }

        externalNativeBuild {
            cmake {
                arguments += listOf("-DCMAKE_CXX_STANDARD=20", "-DANDROID_STL=c++_shared")
            }
        }
    }

    buildTypes {
        release {
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
            version = "3.22.1"
        }
    }

    ndkVersion = "26.1.10909125"

    androidResources {
        noCompress += listOf("tflite")
        ignoreAssetsPatterns += "*.sofa"
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
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.12.0")
    implementation("androidx.appcompat:appcompat:1.6.1")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.7.0")
    implementation("androidx.lifecycle:lifecycle-livedata-ktx:2.7.0")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.7.0")

    implementation("androidx.compose.ui:ui:1.6.0")
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
    implementation("com.google.code.gson:gson:2.14.0")

    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.11.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.11.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-play-services:1.11.0")

    implementation(platform("com.google.firebase:firebase-bom:34.17.0"))
    implementation("com.google.firebase:firebase-firestore")
    implementation("com.google.firebase:firebase-auth")
    implementation("com.google.firebase:firebase-appcheck-debug:19.4.0") {
        exclude(group = "org.jetbrains.kotlinx", module = "kotlinx-coroutines-core")
        exclude(group = "org.jetbrains.kotlinx", module = "kotlinx-coroutines-core-jvm")
        exclude(group = "org.jetbrains.kotlinx", module = "kotlinx-coroutines-android")
        exclude(group = "org.jetbrains.kotlinx", module = "kotlinx-serialization-json")
        exclude(group = "org.jetbrains.kotlinx", module = "kotlinx-serialization-json-jvm")
        exclude(group = "org.jetbrains.kotlinx", module = "kotlinx-serialization-core")
        exclude(group = "org.jetbrains.kotlinx", module = "kotlinx-serialization-core-jvm")
    }

    // Firebase Generative AI (VertexAI en Firebase)
    implementation("com.google.firebase:firebase-vertexai")

    // Legacy Google AI Client (fallback si Firebase no está disponible)
    implementation("com.google.ai.client.generativeai:generativeai:0.9.0")

    implementation("io.coil-kt:coil-compose:2.5.0")
    implementation("io.coil-kt:coil-video:2.5.0")

    testImplementation("junit:junit:4.13.2")
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

configurations.all {
    resolutionStrategy {
        force(
            "org.jetbrains.kotlin:kotlin-stdlib:2.4.20",
            "org.jetbrains.kotlin:kotlin-stdlib-common:2.4.20",
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
    }
}
