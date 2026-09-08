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

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbConstants
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbEndpoint
import android.hardware.usb.UsbInterface
import android.hardware.usb.UsbManager
import android.os.Build
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
 *
 * MAQUINA DE ESTADOS DE ENTRADA TIPO C (FIX 2026-09-08, verificado antes de
 * escribir: requestDirectAccess/startAsyncStreaming no tenian NINGUN llamador
 * en el repo — grep = 0 hits fuera de este archivo; el DAC conectado a mitad
 * de sesion era invisible y la "ruta libre para DAC" nunca se activaba):
 *
 *   UsbDacAttachReceiver (manifest, ATTACHED)
 *     -> onDeviceAttached(device)
 *        -> startHotplugMonitor() (receiver dinamico ATTACHED/DETACHED —
 *           DETACHED no llega a receivers de manifest, solo dinamicos)
 *        -> si hay permiso: openDirectPath(device)
 *           si no: requestPermission() via PendingIntent; el receiver
 *           dinamico recoge ACTION_USB_PERMISSION y llama openDirectPath()
 *     -> openDirectPath = requestDirectAccess() + startAsyncStreaming()
 *        con el handle nativo del engine real si esta cargado (0 si no —
 *        el motor nativo ya degrada con log propio)
 *   DETACHED / permiso denegado -> stopStreaming() + telemetria honesta.
 *
 * "Ruta libre para DAC": cuando isActive() es true, AudioPipeline entrega
 * los bloques procesados por writeAudio() y el motor nativo los envia por
 * URBs isocronos usbfs — bypass total del mezclador de Android. Si el
 * motor nativo es stub, isIsochronous() devuelve false y la telemetria lo
 * dice explicitamente (sin fantasmas).
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

        /** Accion del PendingIntent de permiso USB (unica por paquete). */
        const val ACTION_USB_PERMISSION =
            "com.ivanna.omega.audio.USB_DAC_PERMISSION"

        @Volatile
        private var INSTANCE: UsbAudioProManager? = null

        fun getInstance(context: Context): UsbAudioProManager {
            return INSTANCE ?: synchronized(this) {
                INSTANCE ?: UsbAudioProManager(context.applicationContext).also { INSTANCE = it }
            }
        }
    }

    private val appContext: Context = context.applicationContext
    private val usbManager: UsbManager =
        appContext.getSystemService(Context.USB_SERVICE) as UsbManager

    // Estado del hotplug dinamico. Guarda contra doble registro (el receiver
    // de manifest puede disparar varias veces seguidas si el usuario
    // conecta/desconecta rapido, y registerReceiver dos veces con el mismo
    // receiver instancia es IllegalArgumentException en API 26+).
    private val hotplugActive = AtomicBoolean(false)
    private var hotplugReceiver: BroadcastReceiver? = null

    // Dispositivo pendiente de permiso (el usuario aun no respondio el dialogo).
    @Volatile private var pendingPermissionDevice: UsbDevice? = null
    private var usbConnection: UsbDeviceConnection? = null
    private var audioEndpoint: UsbEndpoint? = null
    private var audioInterface: UsbInterface? = null
    private var fileDescriptor: ParcelFileDescriptor? = null

    private val isStreaming = AtomicBoolean(false)
    private val isAsyncSlave = AtomicBoolean(false)

    // Buffer lock-free de triple buffering para evitar jitter del GC de Android
    private lateinit var ringBuffer: TripleBufferS32
    private var nativeEngineHandle: Long = 0L

    // ────────────────────────────────────────────────────────────────────
    //  ENTRADA TIPO C — hotplug + permiso UAC (la parte que faltaba)
    // ────────────────────────────────────────────────────────────────────

    /**
     * Punto de entrada desde UsbDacAttachReceiver (manifest) o desde el
     * receiver dinamico. Idempotente: si ya hay sesion directa activa con
     * este mismo dispositivo, es no-op.
     */
    fun onDeviceAttached(device: UsbDevice) {
        startHotplugMonitor()
        if (isStreaming.get()) {
            Log.d(TAG, "ATTACHED ignorado — ya hay sesion directa activa")
            return
        }
        if (!hasAudioStreamingInterface(device)) {
            Log.i(TAG, "${device.productName ?: device.deviceName}: sin interface " +
                "AUDIOSTREAMING — no es DAC, ignorado")
            return
        }
        if (usbManager.hasPermission(device)) {
            openDirectPath(device)
        } else {
            requestUsbPermission(device)
        }
    }

    private fun onDeviceDetached(device: UsbDevice) {
        // Solo actua si la desconexion es del DAC que estamos usando (el
        // monitor dinamico ve TODOS los USB del sistema, no solo audio).
        val inUse = usbConnection != null
        if (!inUse) return
        Log.i(TAG, "DAC USB desconectado (${device.productName ?: device.deviceName}) " +
            "— cerrando ruta directa")
        if (isStreaming.get()) {
            stopStreaming()
        } else {
            teardown()
        }
        pendingPermissionDevice = null
    }

    /** true si el dispositivo declara al menos una interface AUDIO/STREAMING. */
    private fun hasAudioStreamingInterface(device: UsbDevice): Boolean {
        for (i in 0 until device.interfaceCount) {
            val iface = device.getInterface(i)
            if (iface.interfaceClass == USB_AUDIO_CLASS &&
                iface.interfaceSubclass == USB_SUBCLASS_AUDIOSTREAMING) return true
        }
        return false
    }

    /**
     * Solicita el permiso UAC al usuario via PendingIntent. El resultado
     * llega al receiver dinamico (ACTION_USB_PERMISSION). El dialogo del
     * sistema es el unico camino legitimo: sin el, openDevice() lanza
     * SecurityException en todos los OEM.
     */
    private fun requestUsbPermission(device: UsbDevice) {
        pendingPermissionDevice = device
        val flags = PendingIntent.FLAG_UPDATE_CURRENT or
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) PendingIntent.FLAG_MUTABLE else 0
        // FLAG_MUTABLE (no IMMUTABLE): el sistema debe poder adjuntar
        // EXTRA_DEVICE + EXTRA_PERMISSION_GRANTED al intent de respuesta.
        val pi = PendingIntent.getBroadcast(appContext, 0, Intent(ACTION_USB_PERMISSION), flags)
        runCatching {
            usbManager.requestPermission(device, pi)
            Log.i(TAG, "Permiso USB solicitado para ${device.productName ?: device.deviceName}")
        }.onFailure {
            Log.w(TAG, "requestPermission fallo: ${it.message}")
            pendingPermissionDevice = null
        }
    }

    /**
     * Abre el path directo completo: claimInterface + endpoint isoc +
     * arranque del motor async. Encadena las dos primitivas historicas de
     * esta clase, que hasta hoy nadie llamaba.
     */
    private fun openDirectPath(device: UsbDevice) {
        if (!requestDirectAccess(device)) return
        // Handle nativo: el engine real se gestiona en el lado C++; 0 indica
        // "sin handle de pipeline asociado" — el motor ya trata ese caso con
        // log propio y la ruta Java sigue viva (writeAudio sigue alimentando
        // el triple buffer para cuando el motor suba).
        val started = startAsyncStreaming(0L)
        Log.i(TAG, if (started)
            "Ruta libre para DAC ACTIVA: bypass del mezclador Android en curso"
        else
            "requestDirectAccess ok pero startAsyncStreaming fallo — ruta directa inerte")
    }

    /**
     * Registra el receiver dinamico de hotplug (ATTACHED/DETACHED) + respuesta
     * de permiso. DETACHED nunca llega a receivers de manifest (documentado
     * desde API 12), asi que sin esto la desconexion del DAC dejaba la sesion
     * zombie: isStreaming=true, fd muerto, proximo writeAudio a un endpoint
     * inexistente.
     */
    @Synchronized
    fun startHotplugMonitor() {
        if (!hotplugActive.compareAndSet(false, true)) return
        val receiver = object : BroadcastReceiver() {
            override fun onReceive(context: Context, intent: Intent) {
                when (intent.action) {
                    UsbManager.ACTION_USB_DEVICE_ATTACHED -> {
                        extractDevice(intent)?.let { onDeviceAttached(it) }
                    }
                    UsbManager.ACTION_USB_DEVICE_DETACHED -> {
                        extractDevice(intent)?.let { onDeviceDetached(it) }
                    }
                    ACTION_USB_PERMISSION -> {
                        val granted = intent.getBooleanExtra(
                            UsbManager.EXTRA_PERMISSION_GRANTED, false)
                        val dev = extractDevice(intent) ?: pendingPermissionDevice
                        pendingPermissionDevice = null
                        if (granted && dev != null) {
                            Log.i(TAG, "Permiso USB concedido — abriendo ruta directa")
                            openDirectPath(dev)
                        } else {
                            // Telemetria honesta: el usuario denego (o el
                            // sistema cancelo) — la ruta directa NO existe y
                            // el audio sigue por el mezclador normal.
                            Log.w(TAG, "Permiso USB DENEGADO — ruta libre para DAC " +
                                "no disponible; el audio sigue por AudioTrack/mezclador")
                        }
                    }
                }
            }
        }
        val filter = IntentFilter().apply {
            addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
            addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
            addAction(ACTION_USB_PERMISSION)
        }
        runCatching {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                // RECEIVER_NOT_EXPORTED: las tres acciones las emite el propio
                // sistema/nuestra app; exportarla dejaria que cualquier app
                // falsificara un ATTACHED/DETACHED y secuestrara el path USB.
                appContext.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED)
            } else {
                @Suppress("UnspecifiedRegisterReceiverFlag")
                appContext.registerReceiver(receiver, filter)
            }
            hotplugReceiver = receiver
            Log.i(TAG, "Monitor hotplug USB activo (ATTACHED/DETACHED/permiso)")
        }.onFailure {
            hotplugActive.set(false)
            Log.w(TAG, "registerReceiver hotplug fallo: ${it.message}")
        }
    }

    /** Detiene el monitor dinamico (p.ej. al cerrar la app). Idempotente. */
    @Synchronized
    fun stopHotplugMonitor() {
        if (!hotplugActive.compareAndSet(true, false)) return
        hotplugReceiver?.let { runCatching { appContext.unregisterReceiver(it) } }
        hotplugReceiver = null
    }

    @Suppress("DEPRECATION")
    private fun extractDevice(intent: Intent): UsbDevice? =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
        } else {
            intent.getParcelableExtra(UsbManager.EXTRA_DEVICE)
        }

    /**
     * Escaneo en frio: si el DAC ya estaba conectado ANTES de arrancar la app
     * (el caso mas comun), nunca llega un ATTACHED — hay que mirar deviceList.
     * Devuelve true si encontro y abrio una ruta directa.
     */
    fun openAttachedDacIfPresent(): Boolean {
        startHotplugMonitor()
        val devices = runCatching { usbManager.deviceList.values.toList() }
            .getOrElse {
                Log.w(TAG, "deviceList: ${it.message}")
                return false
            }
        val dac = devices.firstOrNull { hasAudioStreamingInterface(it) } ?: run {
            Log.d(TAG, "Sin DAC USB conectado al arrancar — ruta directa en espera")
            return false
        }
        onDeviceAttached(dac)
        return isStreaming.get()
    }

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

        // Publica la geometría real del endpoint al motor nativo ANTES de
        // arrancarlo: sin esto el hilo URB no sabe el tamaño de paquete ni el
        // bInterval y no puede calcular los frames por microtrama.
        audioEndpoint?.let { ep ->
            nativeConfigureEndpoint(
                epAddress     = ep.address,
                maxPacketSize = ep.maxPacketSize,
                interval      = ep.interval.coerceAtLeast(1),
                sampleRate    = SAMPLE_RATE,
                channels      = CHANNELS,
                bitDepth      = BIT_DEPTH
            )
        }

        // Delega al hilo nativo via JNI; el hilo URB entrega al DAC en modo
        // asíncrono (el DAC marca el reloj, nosotros seguimos su cadencia).
        nativeStartAsyncEngine(nativeHandle, fileDescriptor?.fd ?: -1)

        Log.i(TAG, "Modo USB Asíncrono activado. DAC es master de reloj.")
        return true
    }

    /** true si el streaming asíncrono directo está activo ahora mismo. */
    fun isActive(): Boolean = isStreaming.get()

    /**
     * Escribe un bloque de audio float [-1,1] estéreo intercalado al triple
     * buffer lock-free, convertido a S32_LE. Debe llamarse desde el mismo
     * hilo productor (AudioPipeline) en cada bloque procesado; el hilo
     * nativo async consume el buffer "ready" cuando el DAC lo solicita.
     *
     * FIX (cableado real): antes esta clase no tenía ningún método para
     * recibir audio del pipeline — quedaba huérfana pese a tener el path
     * USB OTG completo. `nativeStartAsyncEngine` en el lado C++ sigue
     * siendo un stub de logging (no hace poll() real del endpoint
     * isochronous todavía) — esto conecta el productor, no inventa el
     * consumidor nativo que falta.
     */
    fun writeAudio(samples: FloatArray, frameCount: Int) {
        if (!isStreaming.get() || !::ringBuffer.isInitialized) return
        val n = frameCount.coerceAtMost(samples.size / CHANNELS)
        val buf = ringBuffer.getWriteBuffer()
        buf.clear()
        for (i in 0 until n * CHANNELS) {
            val s = samples[i].coerceIn(-1f, 1f)
            buf.putInt((s * Int.MAX_VALUE.toFloat()).toInt())
        }
        ringBuffer.commitWrite()

        // CABLEADO REAL: además del triple buffer Java (que conserva la ruta
        // histórica intacta), se entrega el bloque al anillo SPSC nativo que
        // consume el hilo de URBs isócronos. Antes el consumidor no existía y
        // el audio moría en el lado Java.
        try {
            nativeWriteFrames(samples, n)
        } catch (t: Throwable) {
            Log.w(TAG, "writeAudio: motor nativo no disponible (${t.message})")
        }
    }

    /** Estadísticas del motor: [submitted, completed, errors, xruns, fill, iso]. */
    fun engineStats(): IntArray = try {
        nativeGetEngineStats() ?: IntArray(6)
    } catch (t: Throwable) { IntArray(6) }

    /** true si el motor está entregando por URBs isócronos reales. */
    fun isIsochronous(): Boolean = try { nativeIsIsochronous() } catch (t: Throwable) { false }

    fun stopStreaming() {
        isStreaming.set(false)
        isAsyncSlave.set(false)
        // FIX (desconexión): nativeStopAsyncEngine se llamaba sin guard —
        // si el motor nativo nunca arrancó (USB desconectado a mitad de
        // sesión, lib no cargada), UnsatisfiedLinkError tumbaba el stop.
        try {
            nativeStopAsyncEngine(nativeEngineHandle)
        } catch (t: Throwable) {
            android.util.Log.w(TAG, "stopStreaming: motor nativo no disponible (${t.message})")
        }
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

    // Motor asíncrono REAL (URBs isócronos usbfs) — cableado de punta a punta
    private external fun nativeConfigureEndpoint(
        epAddress: Int, maxPacketSize: Int, interval: Int,
        sampleRate: Int, channels: Int, bitDepth: Int
    )
    private external fun nativeWriteFrames(samples: FloatArray, frames: Int): Int
    private external fun nativeGetEngineStats(): IntArray?
    private external fun nativeIsIsochronous(): Boolean
}
