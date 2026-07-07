package com.ivanna.omega.core

import android.util.Log
import java.util.concurrent.atomic.AtomicBoolean

/**
 * NativeLibraryLoader — Punto único, thread-safe, idempotente para cargar
 * libivanna_omega.so.
 *
 * PROBLEMA HISTÓRICO:
 *   Cinco clases distintas hacían `System.loadLibrary("ivanna_omega")` en
 *   sus propios init blocks:
 *     - DSPBridge (dsp/DSPBridge.kt)
 *     - OmegaEngine (core/OmegaEngine.kt)
 *     - IvannaNativeLib (core/IvannaNativeLib.kt)
 *     - AudioEngine (audio/AudioEngine.kt)
 *     - PiLstmBridge, IvannaSpatialNative, IvannaNpeNative, IvannaVisualizerNative* (varios)
 *
 *   Consecuencias:
 *     * Cada `System.loadLibrary` reentrante genera un WARNING en logcat
 *       ("Native library ... has already been loaded").
 *     * En cold start, la primera clase que se toque paga el coste
 *       completo de dlopen + resolución de símbolos; las demás pagan un
 *       coste pequeño pero medible (lookup en la tabla de libs cargadas).
 *     * Si una nueva clase quiere cargar la lib, el patrón se copia una
 *       vez más — regla de oro violada por acumulación silenciosa.
 *
 * REGLA DE ORO — no borramos, mejoramos:
 *   Este loader es el punto ÚNICO. Las clases existentes no se borran ni
 *   se modifican de forma disruptiva: solo llaman a
 *   `NativeLibraryLoader.ensureLoaded()` desde su init, que es idempotente
 *   (AtomicBoolean) y silencia los reentries. Se preserva la semántica
 *   de "si falla, la clase sigue viviendo pero loaded=false" que tenían
 *   los init blocks originales.
 *
 * Uso desde otras clases:
 *   ```kotlin
 *   object DSPBridge {
 *       private val loaded = NativeLibraryLoader.ensureLoaded()
 *       ...
 *   }
 *   ```
 */
object NativeLibraryLoader {
    private const val TAG = "NativeLibraryLoader"
    private const val LIB_NAME = "ivanna_omega"

    private val attempted = AtomicBoolean(false)

    @Volatile private var loadedInternal = false

    /**
     * Carga libivanna_omega.so si aún no lo está. Retorna true si la lib
     * está disponible tras la llamada, false si el load original falló
     * (UnsatisfiedLinkError). Es seguro llamar desde múltiples clases
     * concurrentemente — solo intenta el load una vez.
     */
    fun ensureLoaded(): Boolean {
        if (attempted.compareAndSet(false, true)) {
            try {
                System.loadLibrary(LIB_NAME)
                loadedInternal = true
                Log.i(TAG, "libivanna_omega.so cargada (primera carga)")
            } catch (e: UnsatisfiedLinkError) {
                loadedInternal = false
                Log.e(TAG, "Fallo al cargar libivanna_omega.so: ${e.message}", e)
            } catch (e: SecurityException) {
                // Puede ocurrir en algunos entornos de test / instrumentación
                loadedInternal = false
                Log.e(TAG, "SecurityException al cargar libivanna_omega.so: ${e.message}", e)
            }
        }
        return loadedInternal
    }

    /**
     * Estado de la carga sin gatillarla. Útil desde código en hot-path
     * que solo quiere consultar si la lib está disponible.
     */
    val isLoaded: Boolean get() = loadedInternal
}
