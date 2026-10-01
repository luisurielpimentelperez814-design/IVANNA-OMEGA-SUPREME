package com.ivanna.omega.ui.viewmodels

import android.app.Application
import android.content.Context
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.ViewModelProvider.AndroidViewModelFactory.Companion.APPLICATION_KEY
import androidx.lifecycle.viewmodel.initializer
import androidx.lifecycle.viewmodel.viewModelFactory
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.core.NativeBridge
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * CochlearInverseViewModel — State owner para el Eje Supremo Neuroacústico.
 *
 * Persiste [cochlearEnabled] y [cochlearIntensity] en SharedPreferences para que
 * la configuración sobreviva a reinicios del servicio/daemon y reinicios del proceso.
 *
 * Ruta de datos:
 *   UI Switch/Slider
 *     → ViewModel setState*()
 *     → JNI nativeSet*() (IvannaNativeLib, lock-free)
 *     → std::atomic<bool/float> en ivanna_omega_jni.cpp
 *     → CochlearActiveInverseEngine::process() en el hot-path de audio
 *
 * No hay LiveData ni corrutinas: los setters son síncronos y rápidos (una
 * escritura atómica). El ViewModel solo es responsable de la persistencia
 * y del StateFlow que la UI observa con collectAsState().
 */
class CochlearInverseViewModel(application: Application) : AndroidViewModel(application) {

    // ── SharedPreferences ────────────────────────────────────────────────────
    private val prefs = application.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    // ── Estado reactivo (Observable desde Compose) ────────────────────────────
    private val _cochlearEnabled = MutableStateFlow(prefs.getBoolean(KEY_ENABLED, false))
    val cochlearEnabled: StateFlow<Boolean> = _cochlearEnabled.asStateFlow()

    private val _cochlearIntensity = MutableStateFlow(
        prefs.getFloat(KEY_INTENSITY, DEFAULT_INTENSITY)
    )
    val cochlearIntensity: StateFlow<Float> = _cochlearIntensity.asStateFlow()

    private val _nativeCochlearActive = MutableStateFlow(false)
    val nativeCochlearActive: StateFlow<Boolean> = _nativeCochlearActive.asStateFlow()

    private val _nativeCochlearIntensity = MutableStateFlow(DEFAULT_INTENSITY)
    val nativeCochlearIntensity: StateFlow<Float> = _nativeCochlearIntensity.asStateFlow()

    init {
        // Restaurar estado al motor nativo en el arranque (p.ej. servicio reiniciado)
        applyToNative(
            enabled = _cochlearEnabled.value,
            intensity = _cochlearIntensity.value
        )
    }

    // ── Setters públicos (llamados desde la UI) ────────────────────────────────

    fun setCochlearEnabled(enabled: Boolean) {
        _cochlearEnabled.value = enabled
        prefs.edit().putBoolean(KEY_ENABLED, enabled).apply()
        NativeBridge.safeSetCochlearInverseEnabled(enabled)
        isNativeCochlearActive()
        getNativeCochlearIntensity()
    }

    fun setCochlearIntensity(intensity: Float) {
        val clamped = intensity.coerceIn(0f, 1f)
        _cochlearIntensity.value = clamped
        prefs.edit().putFloat(KEY_INTENSITY, clamped).apply()
        NativeBridge.safeSetCochlearIntensity(clamped)
        isNativeCochlearActive()
        getNativeCochlearIntensity()
    }

    /** Aplica el estado guardado al motor (llama ambos setters nativos). */
    private fun applyToNative(enabled: Boolean, intensity: Float) {
        NativeBridge.safeSetCochlearIntensity(intensity)
        NativeBridge.safeSetCochlearInverseEnabled(enabled)
        isNativeCochlearActive()
        getNativeCochlearIntensity()
    }

    fun isNativeCochlearActive(): Boolean {
        val active = IvannaNativeLib.guardedNative(false) {
            NativeBridge.safeIsCochlearActive() || IvannaNativeLib.nativeIsCochlearActive()
        }
        _nativeCochlearActive.value = active
        return active
    }

    fun getNativeCochlearIntensity(): Float {
        val current = IvannaNativeLib.guardedNative(_cochlearIntensity.value) {
            val v = NativeBridge.safeGetCochlearIntensity()
            if (v.isFinite()) v else IvannaNativeLib.nativeGetCochlearIntensity()
        }
        _nativeCochlearIntensity.value = current
        return current
    }

    companion object {
        private const val PREFS_NAME       = "ivanna_cochlear_prefs_v1"
        private const val KEY_ENABLED      = "cochlear_enabled"
        private const val KEY_INTENSITY    = "cochlear_intensity"
        private const val DEFAULT_INTENSITY = 0.35f

        val Factory: ViewModelProvider.Factory = viewModelFactory {
            initializer { CochlearInverseViewModel(this[APPLICATION_KEY]!!) }
        }
    }
}
