package com.ivanna.omega.audio

/**
 * DspLoadTierPolicy — tier de HOLOS por carga del DSP (v2.5.426, re-disparado v2.5.429).
 *
 * Problema medido en vivo (Moto G85, 2026-11-12 20:34, v2.5.424, Tidal propio
 * por USB): con HOLOS en 0.50, el DSP cuesta 5.9-6.7 ms/bloque frente a un
 * presupuesto de shed de 5.67 ms, asi que el gobernador de carga pasa el 35%
 * de los bloques en seco (shed=0.352). Cada cambio procesado->seco es un
 * tronido, ~cada 20-30 ms: "puro tronido, casi no hay melodia". El tier
 * termico de HOLOS (24->12 bandas) ya existe, pero solo responde a la
 * TEMPERATURA (ThermalGovernor): con el DSP saturado pero el telefono frio,
 * el tier se queda en 0 (Normal) y el unico alivio es el shed de bloques
 * enteros.
 *
 * v2.5.426: esta politica cerro ese hueco disparando el tier por SHED alto.
 * v2.5.429 CORRECCION (medido en vivo 23:43, v2.5.428): el shed NO refleja
 * la sobrecarga real — con HOLOS p99 13-25 ms (presupuesto 6.67 ms), ORBE
 * p99 8-9 ms y dsp_us 9-11 ms, el shed se quedo en 1.3-1.4% y el tier en 0,
 * asi que HOLOS siguio a 24 bandas y el tronido siguio. El disparador
 * correcto es la CARGA REAL del DSP: la media movil (EMA) del tiempo por
 * bloque (emaUs) dividida por el presupuesto (budgetNs), ambas de
 * nativeGetDspShed()[1] y [3]. Cuando el DSP tarda mas del 85% del
 * presupuesto, HOLOS baja a 12 bandas; mas del 100% (sobre presupuesto),
 * a Limited (difuso off). Degradacion con gracia en vez de tronidos.
 *
 * Histeresis (el tick del ThermalGovernor es de 2 s):
 *   - sube a 1 (Warm, 12 bandas) con carga > 0.85 durante 2 ticks (4 s)
 *   - sube a 2 (Limited, difuso off) con carga > 1.00 durante 2 ticks (4 s)
 *   - baja un nivel con carga baja durante 4 ticks (8 s) desde 2, o 5 ticks
 *     (10 s) desde 1: sesgo a quedarse en el nivel reducido (evita el
 *     flapping 24<->12 bandas, que tambien se oiria).
 *
 * NO hay tier 3 Bypass: IVANNA ofrece HOLOS y HOLOS cumple — apagarlo
 * (bypass) es no entregar lo que se vende. Si el DSP no alcanza, la
 * respuesta es hacer HOLOS mas rapido (reformular/refactorizar), no
 * apagarlo. El tier 3 existio brevemente en la 429 y se ELIMINO por
 * directiva de Esse (2026-11-12 23:56 CST).
 * Kotlin puro (sin Android), igual que ThermalTier: se prueba en JVM.
 */
class DspLoadTierPolicy {

    var tier: Int = 0
        private set

    private var upTicks = 0
    private var downTicks = 0

    /**
     * Un tick (2 s) con esta carga del DSP (emaUs / presupuesto, 1.0 = al
     * 100% del presupuesto); devuelve el tier resultante (0..2).
     */
    fun update(loadFrac: Float): Int {
        val load = if (loadFrac.isNaN()) 0f else loadFrac.coerceIn(0f, 2f)
        val target = when {
            load > UP_TO_LIMITED -> 2
            load > UP_TO_WARM -> 1
            else -> 0
        }
        if (target > tier) {
            downTicks = 0
            if (++upTicks >= UP_TICKS) {
                tier += 1
                upTicks = 0
            }
        } else if (target < tier) {
            upTicks = 0
            val need = if (tier == 1) DOWN_TICKS_LONG else DOWN_TICKS
            if (++downTicks >= need) {
                tier -= 1
                downTicks = 0
            }
        } else {
            upTicks = 0
            downTicks = 0
        }
        return tier
    }

    companion object {
        /** carga > 85% del presupuesto sostenida: HOLOS a 12 bandas. */
        const val UP_TO_WARM = 0.85f
        /** carga > 100% del presupuesto (sobre presupuesto): HOLOS a 12 bandas + difuso off. */
        const val UP_TO_LIMITED = 1.00f
        /** ticks (2 s) para subir un nivel. */
        const val UP_TICKS = 2
        /** ticks para bajar de 2 a 1. */
        const val DOWN_TICKS = 4
        /** ticks para bajar de 1 a 0 (mas lento: evita el flapping). */
        const val DOWN_TICKS_LONG = 5
    }
}
