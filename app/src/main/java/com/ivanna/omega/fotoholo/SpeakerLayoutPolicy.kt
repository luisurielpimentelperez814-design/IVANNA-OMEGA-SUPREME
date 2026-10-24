package com.ivanna.omega.fotoholo

/**
 * Política pura (sin Android) de la cantidad y disposición de bocinas declaradas por el usuario:
 * estéreo (2.0) o surround (varios canales), con o sin subwoofer activo. Generaliza el mecanismo que
 * ya conecta el perfil Genezy MHCPZ1 (`FotoHoloEngine.genezyWidth`) a cualquier sistema declarado.
 *
 * LÍMITE HONESTO (léase antes de usar): el único actuador real al final de esta cadena es
 * `spatial_width` → `StereoWidener::setSpatialWidth` (ver `omega_effect.cpp:380-382` y
 * `ivanna_omega_jni.cpp`). NO existe en este motor un crossover/LFE con consumidor real — el campo
 * `high_cut_hz` del daemon se guarda y se lee de vuelta pero ningún código aplica ese corte a la señal
 * (confirmado por lectura directa de `omega_effect.cpp`: cero ocurrencias fuera de guardar/leer).
 * Por eso esta política SOLO produce un factor de ancho estéreo acotado, igual que Genezy; NO reclama
 * gestión de graves, redirección a subwoofer ni separación de canal LFE real. El campo `subwooferActive`
 * se usa únicamente para decidir CUÁNTO abrir el ancho (con subwoofer activo el sistema ya reparte los
 * graves físicamente, así que el ancho estéreo del resto del espectro puede abrirse un poco más sin
 * riesgo de descompensar el balance de graves que SÍ maneja el hardware externo).
 */
enum class SpeakerMode { STEREO, SURROUND }

data class SpeakerLayout(
    val mode: SpeakerMode = SpeakerMode.STEREO,
    /** Bocinas satélite declaradas (sin contar el subwoofer). 2 en estéreo; 4+ típico en surround. */
    val satelliteCount: Int = 2,
    val subwooferActive: Boolean = false,
) {
    companion object {
        val STEREO_NO_SUB = SpeakerLayout(SpeakerMode.STEREO, 2, false)
    }
}

object SpeakerLayoutPolicy {
    /** Mismo tope que `FotoHoloMath.WIDTH_CEIL` / el rango de `IvannaSpatialEngine.setWidth` (0..1.5). */
    const val WIDTH_CEIL = 1.5f
    private const val MAX_SATELLITES_CONSIDERED = 8

    /**
     * Factor multiplicativo sobre el ancho ya calculado (incluida cualquier modulación Genezy previa).
     * 1.0 = sin cambio. Nunca por debajo de 0.9 ni por encima de 1.15: es un matiz, no un reenrutamiento.
     *
     * Reglas (deterministas, sin heurística oculta):
     * - Estéreo sin subwoofer (el caso por defecto): 1.0, no toca nada.
     * - Surround: más satélites declarados → un poco más de apertura (hasta 4, luego se satura),
     *   porque hay más bocinas físicas repartiendo el campo lateral.
     * - Subwoofer activo: +3 % adicional (los graves ya están separados físicamente; el resto del
     *   espectro puede abrirse un poco más sin desbalancear el conjunto).
     * - Entradas no finitas o fuera de dominio devuelven 1.0 (no tocar nada ante datos inválidos).
     */
    fun widthFactor(layout: SpeakerLayout): Float {
        if (layout.mode == SpeakerMode.STEREO && !layout.subwooferActive) return 1.0f
        val sats = layout.satelliteCount.coerceIn(0, MAX_SATELLITES_CONSIDERED)
        val surroundBonus = if (layout.mode == SpeakerMode.SURROUND)
            (0.02f * (sats - 2).coerceIn(0, 4)) else 0f
        val subBonus = if (layout.subwooferActive) 0.03f else 0f
        return (1.0f + surroundBonus + subBonus).coerceIn(0.9f, 1.15f)
    }

    /** Aplica el factor a un ancho base ya existente y acota al techo real del motor. */
    fun apply(baseWidth: Float, layout: SpeakerLayout): Float {
        if (!baseWidth.isFinite()) return baseWidth
        return (baseWidth * widthFactor(layout)).coerceIn(0f, WIDTH_CEIL)
    }
}
