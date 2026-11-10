/*
 * © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
 * Transporte de frames de sensor reales hacia NeuroSensorGateway (v2.5.376).
 */
package com.ivanna.omega.neuromorphic

import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Frame ya validado, listo para [NeuroSensorGateway.submitFrame]. */
class ParsedNeuroFrame(
    val kind: NeuroSensorGateway.Kind,
    val quality: Float,
    val abrWaveVLatencyMs: Float,
    val fnirsHbO: Float,
    val response: FloatArray,
    val coherence: FloatArray
)

/**
 * Datagrama UDP (little-endian, longitud EXACTA 82 bytes) que un procesador de sensor real
 * (el que convierte EEG/ABR/fNIRS a respuesta por banda) envía a 127.0.0.1:
 *
 *   0..3 "IVNF" · 4 versión=1 · 5 kind (2 EEG, 3 ABR, 4 fNIRS)
 *   6 quality f32 · 10 abrWaveVLatencyMs f32 · 14 fnirsHbO f32
 *   18 response[8] f32 [0..1] · 50 coherence[8] f32 [0..1]
 *
 * Mismo contrato que NeuroFrame del bucle nativo. El timestamp NO viaja: lo pone el receptor al
 * llegar (reloj monotónico del gateway), así un emisor con reloj distinto no puede envenenar el
 * dominio de tiempo. Cualquier desviación devuelve null.
 */
object NeuroFrameCodec {
    const val VERSION = 1
    const val SIZE = 6 + 12 + 8 * 4 + 8 * 4
    private val MAGIC = byteArrayOf('I'.code.toByte(), 'V'.code.toByte(), 'N'.code.toByte(), 'F'.code.toByte())

    fun parse(buf: ByteArray, len: Int): ParsedNeuroFrame? {
        if (len != SIZE || len > buf.size) return null
        for (i in MAGIC.indices) if (buf[i] != MAGIC[i]) return null
        if (buf[4].toInt() != VERSION) return null
        val kind = NeuroSensorGateway.Kind.values().firstOrNull { it.id == buf[5].toInt() } ?: return null
        val bb = ByteBuffer.wrap(buf, 6, len - 6).order(ByteOrder.LITTLE_ENDIAN)
        val quality = bb.float
        val abr = bb.float
        val hbo = bb.float
        val resp = FloatArray(8) { bb.float }
        val coh = FloatArray(8) { bb.float }
        if (!quality.isFinite() || quality < 0f || quality > 1f) return null
        if (!abr.isFinite() || !hbo.isFinite()) return null
        if (resp.any { !it.isFinite() || it < 0f || it > 1f }) return null
        if (coh.any { !it.isFinite() || it < 0f || it > 1f }) return null
        return ParsedNeuroFrame(kind, quality, abr, hbo, resp, coh)
    }

    fun encode(kind: NeuroSensorGateway.Kind, quality: Float, abrMs: Float, hbo: Float,
               response: FloatArray, coherence: FloatArray): ByteArray {
        require(response.size == 8 && coherence.size == 8) { "8 bandas" }
        val bb = ByteBuffer.allocate(SIZE).order(ByteOrder.LITTLE_ENDIAN)
        bb.put(MAGIC).put(VERSION.toByte()).put(kind.id.toByte())
        bb.putFloat(quality).putFloat(abrMs).putFloat(hbo)
        for (v in response) bb.putFloat(v)
        for (v in coherence) bb.putFloat(v)
        return bb.array()
    }
}
