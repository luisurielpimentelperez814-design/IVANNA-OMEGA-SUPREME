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
