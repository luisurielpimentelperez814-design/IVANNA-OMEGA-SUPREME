-keep class com.ivanna.omega.** { *; }
-keepclasseswithmembernames class * {
    native <methods>;
}

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
