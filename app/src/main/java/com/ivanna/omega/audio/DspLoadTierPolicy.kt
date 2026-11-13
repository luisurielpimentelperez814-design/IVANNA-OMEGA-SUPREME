package com.ivanna.omega.audio

/**
 * DspLoadTierPolicy — tier de HOLOS por carga del DSP (v2.5.426).
 *
 * Problema medido en vivo (Moto G85, 2026-11-12 20:34, v2.5.424, Tidal propio
 * por USB): con HOLOS en 0.50, el DSP cuesta 5.9-6.7 ms/bloque frente a un
 * presupuesto de shed de 5.67 ms, asi que el gobernador de carga pasa el 35%
 * de los bloques en seco (shed=0.352). Cada cambio procesado->seco es un
 * tronido, ~cada 20-30 ms: "puro tronido, casi no hay melodia". El tier
 * termico de HOLOS (24->12 bandas) ya existe, pero solo responde a la
 * TEMPERATURA (ThermalGovernor): con el DSP saturado pero el telefono frio,
 * el tier se queda en 0 (Normal) y el unico alivio es el shed de bloques
 * enteros. Esta politica cierra ese hueco: el tier de HOLOS tambien sube
 * por CARGA (shed alto), degradando con gracia (menos bandas) en vez de
 * soltar bloques a seco.
 *
 * Histeresis (el tick del ThermalGovernor es de 2 s):
 *   - sube a 1 (Warm, 12 bandas) con shed > 0.10 durante 2 ticks (4 s)
 *   - sube a 2 (Limited, difuso off) con shed > 0.25 durante 2 ticks (4 s)
 *   - baja un nivel con shed bajo durante 4 ticks (8 s) desde 2, o 5 ticks
 *     (10 s) desde 1: sesgo a quedarse en el nivel reducido (evita el
 *     flapping 24<->12 bandas, que tambien se oiria).
 * Kotlin puro (sin Android), igual que ThermalTier: se prueba en JVM.
 */
class DspLoadTierPolicy {

    var tier: Int = 0
        private set

    private var upTicks = 0
    private var downTicks = 0

    /** Un tick (2 s) con este shed; devuelve el tier resultante (0..2). */
    fun update(shedFrac: Float): Int {
        val shed = if (shedFrac.isNaN()) 0f else shedFrac.coerceIn(0f, 1f)
        val target = when {
            shed > UP_TO_LIMITED -> 2
            shed > UP_TO_WARM -> 1
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
        /** shed > 10% sostenido: HOLOS a 12 bandas. */
        const val UP_TO_WARM = 0.10f
        /** shed > 25% sostenido: HOLOS a 12 bandas + difuso off. */
        const val UP_TO_LIMITED = 0.25f
        /** ticks (2 s) para subir un nivel. */
        const val UP_TICKS = 2
        /** ticks para bajar de 2 a 1. */
        const val DOWN_TICKS = 4
        /** ticks para bajar de 1 a 0 (mas lento: evita el flapping). */
        const val DOWN_TICKS_LONG = 5
    }
}
