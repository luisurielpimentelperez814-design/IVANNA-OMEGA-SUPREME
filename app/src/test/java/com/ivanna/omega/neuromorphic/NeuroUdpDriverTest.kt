/*
 * © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
 * Pruebas del transporte de frames reales (v2.5.376). Sin sensores simulados: bytes fijados a mano
 * y UDP loopback real hacia un sink que solo registra lo recibido.
 */
package com.ivanna.omega.neuromorphic

import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.util.concurrent.CopyOnWriteArrayList
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class NeuroFrameCodecTest {
    private val r = FloatArray(8) { 0.5f }
    private val c = FloatArray(8) { 0.9f }
    private fun good() = NeuroFrameCodec.encode(NeuroSensorGateway.Kind.ABR, 0.8f, 6.5f, 0f, r, c)

    @Test fun goldenHeaderAndSize() {
        val b = good()
        assertEquals(82, b.size)
        assertEquals("49564e46" + "01" + "03" + "cdcc4c3f", b.take(10).joinToString("") { "%02x".format(it) })
    }

    @Test fun roundTrip() {
        val f = NeuroFrameCodec.parse(good(), 82)!!
        assertEquals(NeuroSensorGateway.Kind.ABR, f.kind); assertEquals(0.8f, f.quality, 0f)
        assertEquals(6.5f, f.abrWaveVLatencyMs, 0f); assertEquals(0.5f, f.response[3], 0f)
    }

    @Test fun strictRejections() {
        val b = good()
        assertNull(NeuroFrameCodec.parse(b, 81)); assertNull(NeuroFrameCodec.parse(b + byteArrayOf(0), 83))
        assertNull(NeuroFrameCodec.parse(b.copyOf().also { it[0] = 0 }, 82))
        assertNull(NeuroFrameCodec.parse(b.copyOf().also { it[4] = 2 }, 82))
        assertNull(NeuroFrameCodec.parse(b.copyOf().also { it[5] = 1 }, 82))   // kind 1 (simulado) no existe
        assertNull(NeuroFrameCodec.parse(NeuroFrameCodec.encode(NeuroSensorGateway.Kind.ABR, 1.5f, 0f, 0f, r, c), 82))
        assertNull(NeuroFrameCodec.parse(NeuroFrameCodec.encode(NeuroSensorGateway.Kind.ABR, 0.5f, 0f, 0f, FloatArray(8) { Float.NaN }, c), 82))
        assertNull(NeuroFrameCodec.parse(NeuroFrameCodec.encode(NeuroSensorGateway.Kind.ABR, 0.5f, 0f, 0f, r, FloatArray(8) { 2f }), 82))
    }
}

class NeuroUdpDriverTest {
    @Test fun loopbackDeliversValidAndCountsInvalid() {
        val got = CopyOnWriteArrayList<Pair<ParsedNeuroFrame, Long>>()
        val d = NeuroUdpDriver(udpPort = 0, nowUs = { 12345L }, sink = { f, t -> got += f to t; true })
        d.start()
        assertTrue(d.boundPort != 0)
        val dest = InetAddress.getByAddress(byteArrayOf(127, 0, 0, 1))
        val ok = NeuroFrameCodec.encode(NeuroSensorGateway.Kind.EEG_IN_EAR, 0.9f, 0f, 0f, FloatArray(8) { 0.4f }, FloatArray(8) { 0.8f })
        DatagramSocket().use {
            it.send(DatagramPacket(ok, ok.size, dest, d.boundPort))
            it.send(DatagramPacket(byteArrayOf(1, 2, 3), 3, dest, d.boundPort))
        }
        val end = System.currentTimeMillis() + 5000
        while (d.received.get() < 2 && System.currentTimeMillis() < end) Thread.sleep(10)
        d.stop()
        assertEquals(1, got.size); assertEquals(12345L, got[0].second)
        assertEquals(1L, d.accepted.get()); assertEquals(1L, d.rejected.get()); assertEquals(0, d.boundPort)
        assertNotNull(got[0].first)
    }

    @Test fun stopIsIdempotent() {
        val d = NeuroUdpDriver(udpPort = 0, sink = { _, _ -> true })
        d.start(); d.start(); d.stop(); d.stop()
    }
}
