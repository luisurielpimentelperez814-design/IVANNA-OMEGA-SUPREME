package com.ivanna.omega.spatial

import kotlin.math.sqrt

object AbxStats {
    fun calculateBinomialPValue(k: Int, n: Int, p0: Double = 0.5): Double {
        if (n == 0) return 1.0
        // Usar aproximación normal para n grande o cálculo exacto para n pequeño
        // Simplificado con aproximación normal Z-test para proporciones
        val p = k.toDouble() / n
        val se = sqrt(p0 * (1 - p0) / n)
        if (se == 0.0) return 1.0
        val z = (p - p0) / se
        return 1.0 - normalCdf(z)
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
