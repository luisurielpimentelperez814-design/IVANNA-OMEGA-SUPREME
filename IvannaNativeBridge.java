// ARCHIVO OBSOLETO — movido a:
// app/src/main/java/com/ivanna/omega/supreme/IvannaNativeBridge.java
// Este archivo raíz no forma parte del build de Android Studio.
// FIX aplicados en la ubicación canónica:
//   - AutoCloseable (evita leak nativo si close() no se llama)
//   - processAudioBlock/processDirectBuffer synchronized (race condition)
//   - finalize() como red de seguridad GC
