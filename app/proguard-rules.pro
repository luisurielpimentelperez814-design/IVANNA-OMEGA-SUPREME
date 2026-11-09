# ── Seguridad anti-piratería (v2.5.369): ofuscación ACTIVADA para el código
# de la app. Antes: `-keep class com.ivanna.omega.** { *; }` dejaba TODAS las
# clases y miembros sin ofuscar (el APK decompilado era legible al 100%).
# Ahora se conservan por nombre SOLO los entry points que el framework o la
# reflexión necesitan; el resto del código se ofusca/renombra con R8.
# Verificado antes de escribir estas reglas:
#  - Class.forName por nombre de clase: solo "com.ivanna.omega.MainActivity"
#    (PlaybackCaptureService.kt:295,351) y clases del framework Android
#    (SystemProperties, ActivityThread) que R8 no toca.
#  - Gson: solo deserializa AudioState (ParameterStore.kt:153,200) por nombre
#    de campo (sin @SerializedName) -> se conservan sus miembros y su enum.
#  - JNI: los métodos native se conservan por nombre (regla de abajo) y R8
#    conserva el nombre de la clase que los contiene; las 145 funciones JNI
#    usan JNIEXPORT en C++.
#  - Kotlin serialization, Compose, Firebase/ARCore/TFLite: traen sus propias
#    reglas de consumidor o se cubren con las reglas de abajo.
#  - Componentes del manifest (Application/Activity/Service/Receiver): R8 ya
#    los conserva por las reglas de Android, se listan explícitos por seguridad.
# Si el build de release fallara por una clase que falte, la CI lo detecta:
# el fallback es restaurar el keep global (pero eso vuelve a dejar el código
# sin ofuscar).
-keep public class com.ivanna.omega.MainActivity { *; }
-keep public class com.ivanna.omega.core.IVANNAApplication { *; }
-keep public class com.ivanna.omega.audio.PlaybackCaptureService { *; }
-keep public class com.ivanna.omega.audio.AudioSessionReceiver { *; }
-keep public class com.ivanna.omega.audio.BootRestoreReceiver { *; }
-keep public class com.ivanna.omega.audio.UsbDacAttachReceiver { *; }
-keep public class com.ivanna.omega.fotoholo.RoomArCaptureActivity { *; }
-keep public class com.ivanna.omega.ui.CognitiveDashboardActivity { *; }
-keepclasseswithmembernames class * {
    native <methods>;
}
# Gson: AudioState se deserializa por nombre de campo (sin @SerializedName).
-keepclassmembers class com.ivanna.omega.audio.AudioState { *; }
-keepclassmembers enum com.ivanna.omega.audio.AdaptiveMode { *; }

-dontwarn okio.**
-dontwarn org.tensorflow.**
-dontwarn com.google.**
-dontwarn io.coil.**
-dontwarn androidx.**
-dontwarn kotlinx.**
-keepattributes *Annotation*,Signature,InnerClasses,EnclosingMethod

# ── R8: avisos de clases faltantes/metadata (referencias opcionales de libs) ──
-dontwarn org.tensorflow.lite.gpu.GpuDelegateFactory$Options
-dontwarn org.tensorflow.lite.gpu.GpuDelegateFactory$Options$GpuBackend
-dontwarn com.google.auto.value.**
-dontwarn com.google.errorprone.annotations.**
-dontwarn javax.annotation.**
-dontwarn org.checkerframework.**
-dontwarn org.conscrypt.**
-dontwarn org.bouncycastle.**
-dontwarn org.openjsse.**
-dontwarn kotlin.reflect.jvm.internal.**
-dontwarn java.lang.invoke.StringConcatFactory
-keep class kotlin.Metadata { *; }
-keepattributes RuntimeVisibleAnnotations,RuntimeVisibleParameterAnnotations,KotlinMetadata
-keepclassmembers class **$$serializer { *; }
-keepclassmembers @kotlinx.serialization.Serializable class ** { *** Companion; *** $serializer(...); }
