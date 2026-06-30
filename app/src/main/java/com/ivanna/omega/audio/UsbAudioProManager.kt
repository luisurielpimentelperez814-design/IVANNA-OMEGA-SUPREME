/*
 * ============================================================================
 * IVANNA Singularity V3.0 — Motor de Audio Holográfico de Bajo Nivel
 * ============================================================================
 * Autoría Exclusiva y Propiedad Absoluta:
 *   Luis Uriel Pimentel Pérez (alias Gore TNS)
 *
 * Todos los modelos matemáticos, arquitecturas de sistema y implementaciones
 * de código contenidos en este archivo son propiedad intelectual exclusiva
 * del autor citado. Queda estrictamente prohibida la reproducción, distribución,
 * modificación o uso comercial no autorizado.
 *
 * Este software NO se distribuye bajo licencia CC0 ni dominio público.
 * Todos los derechos reservados. © 2026 Luis Uriel Pimentel Pérez.
 * ============================================================================
 */

package com.ivanna.omega.audio

import android.content.Context
import android.hardware.usb.UsbConstants
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbEndpoint
import android.hardware.usb.UsbInterface
import android.hardware.usb.UsbManager
import android.os.ParcelFileDescriptor
import android.util.Log
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.locks.LockSupport

/**
 * Gestor de acceso USB OTG Directo para DACs de audio USB.
 * Secuestra la ruta de audio evitando completamente el mezclador nativo de Android.
 * Opera en modo de transferencia bulk/isochronous directa hacia el endpoint de audio.
 */
class UsbAudioProManager private constructor(context: Context) {

    companion object {
        private const val TAG = "UsbAudioProManager"
        private const val USB_AUDIO_CLASS = 1
        private const val USB_SUBCLASS_AUDIOCONTROL = 1
        private const val USB_SUBCLASS_AUDIOSTREAMING = 2
        private const val SAMPLE_RATE = 384000
        private const val CHANNELS = 2
        private const val BIT_DEPTH = 32
        private const val FRAME_SIZE_BYTES = (BIT_DEPTH / 8) * CHANNELS

        @Volatile
        private var INSTANCE: UsbAudioProManager? = null

        fun getInstance(context: Context): UsbAudioProManager {
            return INSTANCE ?: synchronized(this) {
                INSTANCE ?: UsbAudioProManager(context.applicationContext).also { INSTANCE = it }
            }
        }
    }

    private val usbManager: UsbManager = context.getSystemService(Context.USB_SERVICE) as UsbManager
    private var usbConnection: UsbDeviceConnection? = null
    private var audioEndpoint: UsbEndpoint? = null
    private var audioInterface: UsbInterface? = null
    private var fileDescriptor: ParcelFileDescriptor? = null

    private val isStreaming = AtomicBoolean(false)
    private val isAsyncSlave = AtomicBoolean(false)

    // Buffer lock-free de triple buffering para evitar jitter del GC de Android
    private lateinit var ringBuffer: TripleBufferS32
    private var nativeEngineHandle: Long = 0L

    /**
     * Solicita acceso USB OTG Directo al dispositivo de audio USB.
     * Escanea interfaces de clase AUDIO y abre conexión raw al endpoint de streaming.
     */
    fun requestDirectAccess(targetDevice: UsbDevice?): Boolean {
        if (targetDevice == null) {
            Log.e(TAG, "Dispositivo USB nulo")
            return false
        }

        if (!usbManager.hasPermission(targetDevice)) {
            Log.e(TAG, "Permiso USB no concedido para ${targetDevice.deviceName}")
            return false
        }

        val connection = usbManager.openDevice(targetDevice) ?: return false
        usbConnection = connection

        // Itera interfaces buscando AUDIOSTREAMING
        for (i in 0 until targetDevice.interfaceCount) {
            val iface = targetDevice.getInterface(i)
            if (iface.interfaceClass == USB_AUDIO_CLASS && 
                iface.interfaceSubclass == USB_SUBCLASS_AUDIOSTREAMING) {

                audioInterface = iface
                connection.claimInterface(iface, true)

                // Busca endpoint isochronous OUT
                for (e in 0 until iface.endpointCount) {
                    val ep = iface.getEndpoint(e)
                    if (ep.type == UsbConstants.USB_ENDPOINT_XFER_ISOC && 
                        ep.direction == UsbConstants.USB_DIR_OUT) {
                        audioEndpoint = ep
                        break
                    }
                }
                break
            }
        }

        if (audioEndpoint == null) {
            Log.e(TAG, "No se encontró endpoint de audio isochronous OUT")
            teardown()
            return false
        }

        // Obtiene file descriptor raw para bypass nativo
        fileDescriptor = connection.fileDescriptor?.let { ParcelFileDescriptor.adoptFd(it) }

        // Inicializa triple buffer lock-free
        ringBuffer = TripleBufferS32(
            capacityFrames = 4096,
            channels = CHANNELS
        )

        Log.i(TAG, "USB OTG Directo establecido: ${targetDevice.deviceName} @ ${SAMPLE_RATE}Hz S32_LE")
        return true
    }

    /**
     * Inicia streaming en modo USB Asíncrono (slave clock).
     * El DAC es master de reloj; nosotros respondemos a sus peticiones de datos.
     */
    fun startAsyncStreaming(nativeHandle: Long): Boolean {
        if (audioEndpoint == null || usbConnection == null) {
            Log.e(TAG, "Conexión USB no inicializada")
            return false
        }

        nativeEngineHandle = nativeHandle
        isStreaming.set(true)
        isAsyncSlave.set(true)

        // Delega al hilo nativo via JNI; el hilo de audio se bloquea en poll() del FD
        nativeStartAsyncEngine(nativeHandle, fileDescriptor?.fd ?: -1)

        Log.i(TAG, "Modo USB Asíncrono activado. DAC es master de reloj.")
        return true
    }

    fun stopStreaming() {
        isStreaming.set(false)
        isAsyncSlave.set(false)
        nativeStopAsyncEngine(nativeEngineHandle)
        teardown()
    }

    private fun teardown() {
        audioInterface?.let { usbConnection?.releaseInterface(it) }
        usbConnection?.close()
        fileDescriptor?.close()
        usbConnection = null
        audioEndpoint = null
        audioInterface = null
        fileDescriptor = null
    }

    /**
     * Triple buffer lock-free S32_LE para evitar pausas del GC.
     */
    private class TripleBufferS32(capacityFrames: Int, channels: Int) {
        private val frameSize = channels * 4
        private val bufferSize = capacityFrames * frameSize

        // Tres buffers planos directos en native heap
        private val buffers = arrayOf(
            ByteBuffer.allocateDirect(bufferSize).order(ByteOrder.LITTLE_ENDIAN),
            ByteBuffer.allocateDirect(bufferSize).order(ByteOrder.LITTLE_ENDIAN),
            ByteBuffer.allocateDirect(bufferSize).order(ByteOrder.LITTLE_ENDIAN)
        )

        @Volatile
        private var writeIndex = 0

        @Volatile
        private var readIndex = 1

        @Volatile
        private var readyIndex = 2

        fun getWriteBuffer(): ByteBuffer = buffers[writeIndex]
        fun getReadBuffer(): ByteBuffer = buffers[readIndex]

        fun commitWrite() {
            val oldReady = readyIndex
            readyIndex = writeIndex
            writeIndex = oldReady
        }

        fun swapRead() {
            val oldWrite = writeIndex
            writeIndex = readIndex
            readIndex = oldWrite
        }
    }

    // JNI native methods
    private external fun nativeStartAsyncEngine(handle: Long, fd: Int)
    private external fun nativeStopAsyncEngine(handle: Long)
}
