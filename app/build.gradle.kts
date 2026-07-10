plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.serialization")
}

android {
    namespace = "com.ivanna.omega"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.ivanna.omega"
        minSdk = 28
        targetSdk = 35
        versionCode = 1800
        versionName = "1.8"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
            signingConfig = signingConfigs.getByName("debug")
        }
        debug {
            isDebuggable = true
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    ndkVersion = "25.1.8937393"

    androidResources {
        noCompress += listOf("tflite")
    }

    buildFeatures {
        compose = true
    }

    composeOptions {
        kotlinCompilerExtensionVersion = "1.5.14"
    }

    packaging {
        jniLibs {
            pickFirsts += listOf("lib/arm64-v8a/libc++_shared.so")
            // Excluir archivos del módulo Magisk que NO deben estar en el APK
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
    implementation("com.google.android.material:material:1.11.0")
    implementation("androidx.compose.ui:ui:1.6.0")
    implementation("androidx.compose.material3:material3:1.2.0")
    implementation("androidx.compose.ui:ui-tooling-preview:1.6.0")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.7.0")
    implementation("androidx.activity:activity-compose:1.8.2")
    implementation("org.tensorflow:tensorflow-lite:2.14.0")
    implementation("org.tensorflow:tensorflow-lite-support:0.4.4")
    implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.6.3")

    // Firebase (sync de perfiles en la nube) — inicializado manualmente via
    // FirebaseOptions.Builder en CloudSyncManager.kt, SIN el plugin
    // com.google.gms.google-services ni google-services.json (no existe un
    // proyecto Firebase real todavia). Esto es una forma soportada y
    // documentada oficialmente por Firebase de usar el SDK sin el plugin;
    // ver comentarios en CloudSyncManager.kt para instrucciones de setup.
    //
    // BoM fijado en 33.1.2 a proposito: Firebase dejo de publicar los
    // modulos "-ktx" (firebase-firestore-ktx, firebase-auth-ktx) a partir
    // del BoM 34.0.0 (jul 2025), migrando esas extensiones a los modulos
    // principales. Si en el futuro se sube el BoM por encima de 34.x, hay
    // que quitar el sufijo "-ktx" de las dos lineas de abajo y ajustar los
    // imports en CloudSyncManager.kt (firestoreSettings/persistentCacheSettings
    // pasan a vivir en com.google.firebase.firestore, no en .ktx).
    implementation(platform("com.google.firebase:firebase-bom:33.1.2"))
    implementation("com.google.firebase:firebase-firestore-ktx")
    implementation("com.google.firebase:firebase-auth-ktx")
    // Necesaria para el .await() de kotlinx.coroutines.tasks sobre
    // com.google.android.gms.tasks.Task (lo que devuelven las llamadas de
    // Firestore/Auth) — no viene incluida transitivamente con lifecycle-ktx.
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-play-services:1.7.1")
}
