package com.ivanna.omega.sensing

/**
 * Fail-closed gate for any future Wi-Fi-derived 3D scene.
 *
 * This deliberately does not fabricate room dimensions, wall materials, people,
 * or anatomy from RSSI. A reconstruction model may run only when its acquisition
 * evidence meets the calibrated multi-link CSI requirements.
 */
object WifiSceneReconstruction {
    data class Anchor(
        val id: String,
        val xMeters: Double,
        val yMeters: Double,
        val zMeters: Double
    )

    data class CaptureEvidence(
        val providerId: String?,
        val csiVerifiedOnDevice: Boolean,
        val calibrationId: String?,
        val anchors: List<Anchor>,
        val synchronizedLinks: Int,
        val validCsiFrames: Int,
        val hasCalibratedRangeOrAngleEstimates: Boolean,
        val modelVersion: String?
    )

    data class Readiness(
        val ready: Boolean,
        val confidenceCeiling: Double,
        val blockers: List<String>
    )

    fun evaluate(evidence: CaptureEvidence): Readiness {
        val blockers = mutableListOf<String>()
        if (!evidence.csiVerifiedOnDevice) blockers += "CSI no verificado en este dispositivo."
        if (evidence.providerId.isNullOrBlank()) blockers += "Falta identificar el proveedor CSI."
        if (evidence.calibrationId.isNullOrBlank()) blockers += "Falta calibración específica de la sala y del hardware."
        if (evidence.anchors.map { it.id }.distinct().size < 4) {
            blockers += "Se requieren al menos cuatro anclas con posiciones conocidas para geometría 3D."
        }
        if (!hasNonCoplanarAnchors(evidence.anchors)) {
            blockers += "Las anclas deben tener geometría 3D no coplanar."
        }
        if (evidence.synchronizedLinks < 4) blockers += "Faltan cuatro enlaces CSI sincronizados."
        if (evidence.validCsiFrames < 256) blockers += "Se requieren al menos 256 tramas CSI válidas para iniciar validación."
        if (!evidence.hasCalibratedRangeOrAngleEstimates) {
            blockers += "No hay estimaciones de rango/ángulo calibradas para resolver geometría."
        }
        if (evidence.modelVersion.isNullOrBlank()) blockers += "Falta versión del modelo de reconstrucción validado."
        return Readiness(
            ready = blockers.isEmpty(),
            confidenceCeiling = if (blockers.isEmpty()) 0.70 else 0.0,
            blockers = blockers
        )
    }

    private fun hasNonCoplanarAnchors(anchors: List<Anchor>): Boolean {
        val unique = anchors.distinctBy { it.id }
        if (unique.size < 4) return false
        val a = unique[0]
        for (i in 1 until unique.size - 2) {
            val b = unique[i]
            for (j in i + 1 until unique.size - 1) {
                val c = unique[j]
                for (k in j + 1 until unique.size) {
                    val d = unique[k]
                    val abx = b.xMeters - a.xMeters
                    val aby = b.yMeters - a.yMeters
                    val abz = b.zMeters - a.zMeters
                    val acx = c.xMeters - a.xMeters
                    val acy = c.yMeters - a.yMeters
                    val acz = c.zMeters - a.zMeters
                    val adx = d.xMeters - a.xMeters
                    val ady = d.yMeters - a.yMeters
                    val adz = d.zMeters - a.zMeters
                    val crossX = aby * acz - abz * acy
                    val crossY = abz * acx - abx * acz
                    val crossZ = abx * acy - aby * acx
                    val volume6 = crossX * adx + crossY * ady + crossZ * adz
                    if (kotlin.math.abs(volume6) > 1e-4) return true
                }
            }
        }
        return false
    }
}
