// ARCHIVO OBSOLETO — movido a:
// app/src/main/java/com/ivanna/omega/ui/ControlTabScreen.kt
//
// Este archivo raíz no forma parte del build de Android Studio.
// Difiere de la versión canónica en 3 fixes críticos que aquí FALTAN:
//   1. npeBypassState se restaura desde ParameterStore (KEY_NPE_BYPASS)
//      — sin eso el toggle volvía a false aunque el usuario lo activara
//   2. LaunchedEffect restaura el bypass al motor nativo al arrancar
//   3. JNI calls envueltos en guardedNative (evita UnsatisfiedLinkError
//      si la librería nativa no cargó aún)
