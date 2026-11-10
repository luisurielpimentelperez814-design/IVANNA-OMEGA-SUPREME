/*
 * © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
 * Driver de sensor real por UDP loopback para NeuroSensorGateway (v2.5.376).
 */
package com.ivanna.omega.neuromorphic

import java.io.IOException
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.SocketAddress
import java.net.SocketTimeoutException
import java.util.concurrent.atomic.AtomicLong

/**
 * Driver que escucha SOLO en 127.0.0.1 y reenvía al gateway los frames "IVNF" de un procesador de
 * sensor real externo (placa EEG/ABR/fNIRS). No genera datos: sin emisor no llega nada y el bucle
 * nativo permanece fail-closed. Riesgo conocido: otra app local podría inyectar frames; el daño
 * queda acotado por los límites duros del bucle nativo (±6 dB, slew, wet).
 */
class NeuroUdpDriver(
    private val udpPort: Int = DEFAULT_PORT,
    override val kind: NeuroSensorGateway.Kind = NeuroSensorGateway.Kind.EEG_IN_EAR,
    private val nowUs: () -> Long = { NeuroSensorGateway.nowUs() },
    private val sink: (ParsedNeuroFrame, Long) -> Boolean = { f, t ->
        NeuroSensorGateway.submitFrame(f.kind, t, f.response, f.coherence, f.quality, f.abrWaveVLatencyMs, f.fnirsHbO)
    }
) : NeuroSensorGateway.Driver {

    val received = AtomicLong()
    val accepted = AtomicLong()
    val rejected = AtomicLong()
    @Volatile var boundPort = 0
        private set
    @Volatile private var thread: Thread? = null
    @Volatile private var socket: DatagramSocket? = null

    @Synchronized override fun start() {
        if (thread?.isAlive == true) return
        val loopback = InetAddress.getByAddress(byteArrayOf(127, 0, 0, 1))
        val requested = udpPort   // no usar `port` dentro de apply{}: DatagramSocket.getPort() lo sombrea (-1)
        val s = try {
            DatagramSocket(null as SocketAddress?).apply {
                soTimeout = TIMEOUT_MS
                bind(InetSocketAddress(loopback, requested))
            }
        } catch (e: IOException) { return }
        socket = s
        boundPort = s.localPort
        thread = Thread({ loop(s) }, "neuro-udp").apply { isDaemon = true; start() }
    }

    @Synchronized override fun stop() {
        val t = thread
        thread = null
        socket?.close(); socket = null
        boundPort = 0
        t?.join(1000)
    }

    private fun loop(s: DatagramSocket) {
        val buf = ByteArray(NeuroFrameCodec.SIZE + 1)
        val pkt = DatagramPacket(buf, buf.size)
        while (!s.isClosed) {
            try { s.receive(pkt) } catch (_: SocketTimeoutException) { continue } catch (_: IOException) { return }
            received.incrementAndGet()
            val f = NeuroFrameCodec.parse(buf, pkt.length)
            pkt.length = buf.size
            if (f != null && sink(f, nowUs())) accepted.incrementAndGet() else rejected.incrementAndGet()
        }
    }

    companion object {
        const val DEFAULT_PORT = 9871
        private const val TIMEOUT_MS = 500
    }
}
