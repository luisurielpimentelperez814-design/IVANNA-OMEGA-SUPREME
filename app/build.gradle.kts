import java.util.Properties

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
        // Unified Version Manager: lee de version.properties (fuente única)
        val vp = Properties()
        rootProject.file("version.properties").inputStream().use { stream -> vp.load(stream) }
        versionCode = vp.getProperty("versionCode").toInt()
        versionName = vp.getProperty("versionName")

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        // FIX (2026-09-02, Gemini 2.5): inyectar la API key del asistente al
        // build. Fuentes en cascada: propiedad -P / variable de entorno
        // GEMINI_API_KEY (CI) → vacío en builds locales (el usuario la
        // ingresa en Ajustes y se persiste en SharedPreferences; ver
        // IvannaGeminiAgent.resolveApiKey()). Nunca se hardcodea una key real.
        val geminiKey = (findProperty("GEMINI_API_KEY") as String?)
            ?: System.getenv("GEMINI_API_KEY")
            ?: ""
        buildConfigField("String", "GEMINI_API_KEY", "\"$geminiKey\"")

        // ABIs soportados: arm64-v8a (principal, Moto G85) + armeabi-v7a (legado 32-bit).
        // x86/x86_64 excluidos: sin target emulador/Intel, ahorra ~40% de tamaño nativo.
        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a")
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
            // signingConfig configurado via IVANNA_KEYSTORE_* env vars en CI/distribución
            // No usar debug key en release — inaceptable para distribución OEM
        }
        debug {
            isDebuggable = true
            // Minify también en debug para reducir tamaño de descarga
            isMinifyEnabled = false
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

    ndkVersion = "26.1.10909125"

    androidResources {
        noCompress += listOf("tflite")
    }

    buildFeatures {
        compose = true
        buildConfig = true
    }

    // Las clases de la capa conversacional usan android.util.Log; sin esto los
    // tests JVM fallan con "not mocked" en vez de ejercitar la lógica real.
    testOptions {
        unitTests.isReturnDefaultValues = true
    }

    composeOptions {
        kotlinCompilerExtensionVersion = "1.5.14"
    }

    packaging {
        jniLibs {
            // FIX (APK corrupto / libs no cargadas):
            // AGP 4.0+ empaqueta .so sin comprimir por defecto (más eficiente en
            // instalaciones modernas). Sin useLegacyPackaging = false explícito,
            // el comportamiento depende de cómo se construyó el APK y qué version
            // de AGP está activa — en algunos entornos AGP comprimía las .so y en
            // otros no, provocando que el package manager del dispositivo recibiera
            // un APK internamente inconsistente (algunas libs comprimidas, otras no)
            // → instalación parcialmente corrupta → System.loadLibrary() fallaba
            // con "file not found" o "ELF header corrupt" aunque el APK se había
            // instalado sin error visible.
            //
            // useLegacyPackaging = false garantiza que TODAS las .so van sin
            // comprimir, que es la forma correcta para minSdk ≥ 23 (soportado
            // desde Android 6). El Manifest lleva android:extractNativeLibs="false"
            // para que el package manager las mapee en memoria directamente sin
            // extraerlas a /data/app — menor uso de disco, carga más rápida.
            useLegacyPackaging = false
            pickFirsts += listOf("lib/arm64-v8a/libc++_shared.so")
            // Excluir libomega_effect.so — es un GlobalEffect para Magisk/AudioFlinger,
            // NO debe ir en el APK (se instala vía magisk_module/system/vendor/lib64/soundfx/)
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
    implementation("androidx.security:security-crypto-ktx:1.1.0-alpha06")

    implementation("androidx.compose.material:material-icons-extended")
    testImplementation("junit:junit:4.13.2")
    implementation("androidx.media:media:1.7.0")
    implementation("androidx.core:core-ktx:1.12.0")
    implementation("androidx.appcompat:appcompat:1.6.1")
    implementation("com.google.android.material:material:1.11.0")
    implementation("androidx.compose.ui:ui:1.6.0")
    implementation("androidx.compose.material3:material3:1.2.0")
    // FIX (build): IvannaAppShell usa Icons.Filled.{RadioButtonChecked, Tune,
    // BlurOn, Psychology, PlayCircle}. Ninguno de los cinco vive en
    // material-icons-core (el set minimo que arrastra material3): son todos
    // del set extendido, que no estaba declarado -> "Unresolved reference".
    // Se anade la dependencia en vez de sustituir los glifos: cambiarlos por
    // iconos de core degrada la semantica de la barra de navegacion, y los
    // reemplazos "obvios" (GraphicEq, Memory) tampoco estan en core, con lo
    // que el build seguiria roto. R8 descarta los iconos no usados en release.
    implementation("androidx.compose.material:material-icons-extended:1.6.0")
    implementation("androidx.compose.ui:ui-tooling-preview:1.6.0")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.7.0")
    // FIX (bb4fa6b): AudioStateManager.kt y VoiceProtectionManager.kt usan
    // androidx.lifecycle.LiveData/MutableLiveData. lifecycle-runtime-ktx NO
    // trae esas clases (son un artefacto distinto) — faltaba, el build de
    // esos dos archivos truena con "unresolved reference: LiveData".
    implementation("androidx.lifecycle:lifecycle-livedata-ktx:2.7.0")
    // FIX: AdaptiveEngineScreen.kt usa LiveData.observeAsState() en Compose
    // -- eso vive en un artefacto aparte (runtime-livedata), no en
    // compose.runtime ni en lifecycle-livedata-ktx. Mismo tipo de bug que
    // los anteriores (dependencia usada pero nunca declarada).
    implementation("androidx.compose.runtime:runtime-livedata:1.6.0")
    implementation("androidx.activity:activity-compose:1.8.2")
    implementation("androidx.navigation:navigation-compose:2.7.7")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.7.0")
    implementation("org.tensorflow:tensorflow-lite:2.14.0")
    implementation("org.tensorflow:tensorflow-lite-support:0.4.4")
    implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.6.3")
    // FIX (bb4fa6b): com/ivanna/omega/audio/ParameterStore.kt (el nuevo,
    // NO el de core/) usa com.google.gson.Gson para serializar AudioState —
    // no estaba declarada, mismo tipo de bug que rompió el resample() antes.
    implementation("com.google.code.gson:gson:2.10.1")

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
    implementation("com.google.ai.client.generativeai:generativeai:0.9.0")
    // Firebase AI Logic (migración Gemini, ver GeminiOrchestrator.kt) — versión
    // explícita, SIN el BoM de arriba a propósito: ese BoM está fijado en
    // 33.1.2 solo por los módulos "-ktx" de Firestore/Auth (ver comentario en
    // esa sección); firebase-ai es un artefacto independiente y no necesita
    // ese BoM ni el plugin com.google.gms.google-services (init manual, ver
    // CloudSyncManager.ensureFirebaseAppReady()).
    implementation("com.google.firebase:firebase-ai:17.12.1")

    // Carga de imagenes/video en Compose (antes en un segundo bloque
    // dependencies {} duplicado que redeclaraba security-crypto-ktx).
    implementation("io.coil-kt:coil-compose:2.5.0")
    implementation("io.coil-kt:coil-video:2.5.0")
}

// Unified Version Manager — validación: module.prop debe coincidir con
// version.properties; si diverge, el build FALLA con mensaje claro.
tasks.register("validateUnifiedVersion") {
    doLast {
        val vp = Properties()
        rootProject.file("version.properties").inputStream().use { stream -> vp.load(stream) }
        val mp = rootProject.file("magisk_module/module.prop").readLines()
        val moduleVersion = mp.firstOrNull { it.startsWith("version=") }?.substringAfter("=")?.removePrefix("v")
        val moduleCode = mp.firstOrNull { it.startsWith("versionCode=") }?.substringAfter("=")
        check(moduleVersion == vp.getProperty("versionName") && moduleCode == vp.getProperty("versionCode")) {
            "❌ DESALINEACIÓN DE VERSIÓN: version.properties=${vp.getProperty("versionName")}(${vp.getProperty("versionCode")}) " +
            "pero module.prop=${moduleVersion}(${moduleCode}). Edita version.properties y sincroniza module.prop."
        }
        logger.lifecycle("✅ Unified Version: ${vp.getProperty("versionName")} (${vp.getProperty("versionCode")}) — APK == módulo")
    }
}
tasks.named("preBuild") { dependsOn("validateUnifiedVersion") }

// FIX (build rojo, 2026-09-05): compileDebugKotlin fallaba en 4 archivos
// (IvannaMemoryArchitecture.kt, IvannaAdaptivePresetEngine.kt,
// ProfileManager.kt, ProfilesLoader.kt) con "Your current Kotlin version
// is 1.9.24, while kotlinx.serialization core runtime 1.7.3 requires at
// least Kotlin 2.0.0-RC1" — verificado leyendo el log real de Gradle
// (compileDebugKotlin FAILED), no adivinado.
//
// kotlinx-serialization-json ESTÁ declarado explícito arriba en 1.6.3
// (compatible con Kotlin 1.9.24), pero Gradle resuelve por defecto a la
// versión MÁS ALTA solicitada por cualquier dependencia del grafo —
// firebase-ai:17.12.1 (agregado en la migración de Gemini a Firebase AI
// Logic) trae kotlinx-serialization-core 1.7.3 transitivamente y le gana
// a la versión explícita. Forzar el downgrade a 1.6.3 en TODO el grafo
// de dependencias es el fix correcto: no requiere subir Kotlin a 2.0+
// (cambio de compilador K2 con blast radius enorme sobre 188+ archivos
// .kt, no justificado solo para esto) y no toca la línea que ya declara
// 1.6.3 explícitamente arriba — solo evita que otra dependencia la pise.
// FIX (build rojo, continuación de 2026-09-05, verificado en log de CI
// POSTERIOR al fix anterior — 2026-09-05T18:04-18:08): el force() de abajo
// solo cubría kotlinx-serialization-*, pero el mismo síntoma ("Class X was
// compiled with an incompatible version of Kotlin") reapareció para TODO
// el árbol base de Kotlin — kotlin.Unit, kotlinx.coroutines.flow.StateFlow,
// kotlin.coroutines.CoroutineContext, kotlin.reflect.KProperty,
// kotlin.enums.EnumEntries — en prácticamente cada archivo .kt del módulo
// (metadata real 2.2.0/2.3.0, compilador 1.9.0 solo lee hasta 2.0.0).
//
// Misma causa raíz que la ya diagnosticada: firebase-ai:17.12.1 no declara
// BOM propio y arrastra transitivamente kotlin-stdlib y
// kotlinx-coroutines-core en versiones más nuevas que el Kotlin 1.9.24
// declarado en build.gradle.kts raíz, y Gradle resuelve por defecto a la
// más alta solicitada por cualquier dependencia del grafo.
//
// Se extiende el mismo force() (no se reemplaza) a los dos artefactos que
// el log real señala: kotlin-stdlib a la versión exacta del plugin Kotlin
// del proyecto (1.9.24), y kotlinx-coroutines-core a 1.7.1 — la misma
// serie que kotlinx-coroutines-play-services ya declarado explícito arriba,
// compatible con Kotlin 1.9.x sin requerir subir a K2/2.0+.
configurations.all {
    resolutionStrategy {
        force(
            "org.jetbrains.kotlinx:kotlinx-serialization-core:1.6.3",
            "org.jetbrains.kotlinx:kotlinx-serialization-json:1.6.3",
            "org.jetbrains.kotlin:kotlin-stdlib:1.9.24",
            "org.jetbrains.kotlin:kotlin-stdlib-common:1.9.24",
            "org.jetbrains.kotlinx:kotlinx-coroutines-core:1.7.1",
            "org.jetbrains.kotlinx:kotlinx-coroutines-android:1.7.1"
        )
    }
}
