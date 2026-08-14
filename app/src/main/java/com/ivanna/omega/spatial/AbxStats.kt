package com.ivanna.omega.spatial

import kotlin.math.sqrt

object AbxStats {
    fun calculateBinomialPValue(k: Int, n: Int, p0: Double = 0.5): Double =
        binomialTest(k, n, p0)

    /**
     * Test binomial EXACTO bilateral (H0: chance = p0).
     * p = 2 · min(P(X≤k), P(X≥k)), recortado a 1.0.
     * Sin librerías externas: términos vía log-gamma para estabilidad.
     */
    fun binomialTest(k: Int, n: Int, p0: Double = 0.5): Double {
        if (n <= 0) return 1.0
        val kk = k.coerceIn(0, n)
        fun logC(i: Int) =
            lgamma(n + 1.0) - lgamma(i + 1.0) - lgamma(n - i + 1.0)
        fun pmf(i: Int): Double {
            if (i < 0 || i > n) return 0.0
            val lp = logC(i) + i * kotlin.math.ln(p0) +
                     (n - i) * kotlin.math.ln(1.0 - p0)
            return kotlin.math.exp(lp)
        }
        var left = 0.0
        for (i in 0..kk) left += pmf(i)          // P(X ≤ k)
        var right = 0.0
        for (i in kk..n) right += pmf(i)         // P(X ≥ k)
        return (2.0 * minOf(left, right)).coerceAtMost(1.0)
    }

    /** Intervalo de confianza 95% (Wilson) para la proporción hits/total. */
    fun wilsonInterval95(hits: Int, total: Int): Pair<Double, Double> {
        if (total <= 0) return 0.0 to 1.0
        val z = 1.959963984540054
        val p = hits.toDouble() / total
        val denom = 1.0 + z * z / total
        val centre = (p + z * z / (2.0 * total)) / denom
        val half = z * sqrt(p * (1 - p) / total + z * z / (4.0 * total * total)) / denom
        return (centre - half).coerceIn(0.0, 1.0) to (centre + half).coerceIn(0.0, 1.0)
    }

    private fun lgamma(x: Double): Double {
        // Lanczos g=7 — suficiente para n ≤ 10000
        val c = doubleArrayOf(0.99999999999980993, 676.5203681218851,
            -1259.1392167224028, 771.32342877765313, -176.61502916214059,
            12.507343278686905, -0.13857109526572012, 9.9843695780195716e-6,
            1.5056327351493116e-7)
        if (x < 0.5) return kotlin.math.ln(Math.PI /
            (kotlin.math.sin(Math.PI * x) * kotlin.math.exp(lgamma(1.0 - x))))
        val xx = x - 1.0
        var a = c[0]
        for (i in 1..8) a += c[i] / (xx + i)
        val t = xx + 7.5
        return 0.5 * kotlin.math.ln(2.0 * Math.PI) +
               (xx + 0.5) * kotlin.math.ln(t) - t + kotlin.math.ln(a)
    }

    fun calculateTTest(scoresA: List<Double>, scoresB: List<Double>): Double {
        if (scoresA.isEmpty() || scoresB.isEmpty() || scoresA.size != scoresB.size) return 1.0
        val n = scoresA.size
        val diffs = scoresA.zip(scoresB).map { it.first - it.second }
        val meanDiff = diffs.average()
        val variance = diffs.map { (it - meanDiff) * (it - meanDiff) }.sum() / (n - 1)
        val t = meanDiff / sqrt(variance / n)
        // Simplificado
        return t
    }
    
    fun calculateCohensD(scoresA: List<Double>, scoresB: List<Double>): Double {
        if (scoresA.isEmpty() || scoresB.isEmpty()) return 0.0
        val meanA = scoresA.average()
        val meanB = scoresB.average()
        val varA = scoresA.map { (it - meanA) * (it - meanA) }.sum() / scoresA.size
        val varB = scoresB.map { (it - meanB) * (it - meanB) }.sum() / scoresB.size
        val pooledSD = sqrt((varA + varB) / 2.0)
        return if (pooledSD > 0) (meanA - meanB) / pooledSD else 0.0
    }

    private fun normalCdf(z: Double): Double {
        val b1 = 0.31938153
        val b2 = -0.356563782
        val b3 = 1.781477937
        val b4 = -1.821255978
        val b5 = 1.330274429
        val p = 0.2316419
        val c = 0.39894228
        val a = Math.abs(z)
        val t = 1.0 / (1.0 + a * p)
        val b = c * Math.exp(-z * z / 2.0)
        val n = ((((b5 * t + b4) * t + b3) * t + b2) * t + b1) * t
        var result = 1.0 - b * n
        if (z < 0.0) result = 1.0 - result
        return result
    }
}
