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

        // Usage Type de un endpoint isocrono (bits 5:4 de bEndpointAttributes,
        // USB Audio spec): 00=Data, 01=Feedback, 10=Implicit-feedback-Data.
        // Sin distinguir esto, cualquier ISOC+IN que el DAC exponga (poco
        // comun pero legal) se confundiria con el endpoint de feedback real.
        private const val USB_ENDPOINT_USAGE_FEEDBACK = 0x1
        private const val CHANNELS = 2
        private const val BIT_DEPTH = 32
        private const val FRAME_SIZE_BYTES = (BIT_DEPTH / 8) * CHANNELS

        // Capacidad del triple buffer Java (frames). Nombrada para que
        // writeAudio pueda acotar el bloque sin hardcodear el 4096 dos veces.
        private const val RING_CAPACITY_FRAMES = 4096

        // Techo de diseno (UAC2 high-speed): 384kHz S32_LE estereo.
        // NO es lo que se negocia — es el limite superior. La SR real se
        // deriva del maxPacketSize del endpoint en negotiateSampleRate().
        private const val MAX_SAMPLE_RATE = 384000
        private const val MIN_SAMPLE_RATE = 44100

        // Tasas estandar UAC en orden descendente — se elige la mayor que
        // quepa en el presupuesto de paquete del endpoint.
        private val STANDARD_SAMPLE_RATES = intArrayOf(
            384000, 352800, 192000, 176400, 96000, 88200, 48000, 44100
        )

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

    // Identidad del DAC con sesion abierta (deviceId es estable por conexion).
    // Sin esto, onDeviceDetached cerraba la sesion ante la desconexion de
    // CUALQUIER USB (un raton, un pendrive) si habia una abierta.
    @Volatile private var openDeviceId: Int = -1
    private var usbConnection: UsbDeviceConnection? = null
    private var audioEndpoint: UsbEndpoint? = null
    // Endpoint de feedback UAC (isoc IN, usage-type Feedback). Null si el DAC
    // no lo expone (valido — muchos UAC1 y algunos UAC2 simples no lo hacen):
    // el motor nativo sigue con la ruta fija actual, sin regresion.
    private var feedbackEndpoint: UsbEndpoint? = null
    private var audioInterface: UsbInterface? = null
    private var fileDescriptor: ParcelFileDescriptor? = null

    private val isStreaming = AtomicBoolean(false)
    private val isAsyncSlave = AtomicBoolean(false)

    // SR negociada con ESTE endpoint (ver negotiateSampleRate). 0 = aun no
    // negociada. Antes era una constante de 384kHz para todos los DACs: un
    // UAC1 full-speed (maxPacketSize tipico 1023B => ~85 frames de S32_LE
    // estereo por microtrama => ~85kHz de techo) recibia una configuracion
    // que fisicamente no cabe en su bus.
    @Volatile private var negotiatedSampleRate = 0

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

    @Synchronized
    private fun onDeviceDetached(device: UsbDevice) {
        // Solo actua si la desconexion es EXACTAMENTE del DAC en uso (el
        // monitor dinamico ve TODOS los USB del sistema: un raton o un
        // pendrive desconectado NO debe cerrar la sesion de audio).
        // Se compara por deviceId (estable durante la conexion) y, como
        // respaldo, por vid/pid — el deviceId puede reasignarse si el DAC
        // se reconecta muy rapido dentro de la misma ventana de eventos.
        val inUse = usbConnection != null
        if (!inUse) {
            // Sin sesion abierta: si era el dispositivo pendiente de permiso,
            // el dialogo queda obsoleto (usuario desconecto antes de decidir).
            if (pendingPermissionDevice?.deviceId == device.deviceId) {
                pendingPermissionDevice = null
                Log.i(TAG, "DAC pendiente de permiso desconectado antes de decidir")
            }
            return
        }
        val isOurDac = device.deviceId == openDeviceId ||
            (openDeviceId < 0 && hasAudioStreamingInterface(device))
        if (!isOurDac) {
            Log.d(TAG, "DETACHED de otro USB (${device.productName ?: device.deviceName}) — sesion intacta")
            return
        }
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
    // @Synchronized: el receiver de manifest, el receiver dinamico y el
    // escaneo en frio pueden disparar openDirectPath desde hilos distintos
    // casi a la vez; sin serializacion, dos de ellos pasaban el guard de
    // sesion antes de que cualquiera la marcara -> doble openDevice +
    // doble claimInterface sobre el mismo DAC (EBUSY o estado incoherente).
    @Synchronized
    fun requestDirectAccess(targetDevice: UsbDevice?): Boolean {
        if (targetDevice == null) {
            Log.e(TAG, "Dispositivo USB nulo")
            return false
        }

        // Re-apertura sobre una sesion ya abierta con el MISMO dispositivo:
        // teardown previo para no apilar conexiones ni fds duplicados.
        if (usbConnection != null) {
            if (openDeviceId == targetDevice.deviceId) {
                Log.i(TAG, "Re-apertura del mismo DAC — teardown previo")
                teardown()
            } else {
                Log.w(TAG, "Ya hay sesion con otro DAC — la nueva solicitud espera " +
                    "a que se cierre la actual")
                return false
            }
        }

        if (!usbManager.hasPermission(targetDevice)) {
            Log.e(TAG, "Permiso USB no concedido para ${targetDevice.deviceName}")
            return false
        }

        val connection = usbManager.openDevice(targetDevice) ?: return false
        usbConnection = connection
        openDeviceId = targetDevice.deviceId

        // Itera interfaces buscando AUDIOSTREAMING
        for (i in 0 until targetDevice.interfaceCount) {
            val iface = targetDevice.getInterface(i)
            if (iface.interfaceClass == USB_AUDIO_CLASS && 
                iface.interfaceSubclass == USB_SUBCLASS_AUDIOSTREAMING) {

                // claimInterface devuelve false si otra app tiene el DAC
                // ocupado (p.ej. un player USB dedicado). Antes el retorno se
                // ignoraba: se seguia como si la interfaz fuera nuestra y el
                // primer URB reventaba contra usbfs con EBUSY.
                if (!connection.claimInterface(iface, true)) {
                    Log.e(TAG, "claimInterface fallo — DAC ocupado por otra app " +
                        "o permiso revocado a mitad de secuencia")
                    teardown()
                    return false
                }
                audioInterface = iface

                // Busca endpoint isochronous OUT (datos) y, si el DAC lo
                // expone, el de feedback (isoc IN, usage-type Feedback). El
                // 'break' original solo cortaba al hallar el OUT — con eso
                // un feedback declarado DESPUES en la lista de endpoints
                // nunca se habria visto. Ahora se recorren todos.
                for (e in 0 until iface.endpointCount) {
                    val ep = iface.getEndpoint(e)
                    if (ep.type != UsbConstants.USB_ENDPOINT_XFER_ISOC) continue
                    if (ep.direction == UsbConstants.USB_DIR_OUT) {
                        if (audioEndpoint == null) audioEndpoint = ep
                    } else if (ep.direction == UsbConstants.USB_DIR_IN) {
                        val usageType = (ep.attributes shr 4) and 0x3
                        if (usageType == USB_ENDPOINT_USAGE_FEEDBACK) {
                            feedbackEndpoint = ep
                        }
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

        // Negociacion de capacidades REALES: la SR se deriva del presupuesto
        // de paquete del endpoint, no de una constante. Si ninguna tasa
        // estandar cabe (endpoint truncado por hub, descriptor raro), se
        // aborta con telemetria honesta en vez de configurar basura.
        val ep = audioEndpoint!!
        negotiatedSampleRate = negotiateSampleRate(ep)
        if (negotiatedSampleRate == 0) {
            Log.e(TAG, "Endpoint isoc sin presupuesto para ninguna SR estandar " +
                "(maxPacketSize=${ep.maxPacketSize}, interval=${ep.interval}) — " +
                "ruta directa no viable con este DAC")
            teardown()
            return false
        }

        // File descriptor para bypass nativo. fromFd() hace dup(2): obtenemos
        // una copia propia SIN robar la propiedad del fd de la conexion.
        // Antes era adoptFd(): el mismo fd quedaba con DOS duenos, y teardown()
        // lo cerraba dos veces (connection.close() + fileDescriptor.close()) —
        // doble close: si entre medias el kernel reasigna ese numero de fd a
        // otro hilo, el segundo close cierra un descriptor ajeno.
        fileDescriptor = connection.fileDescriptor?.let { rawFd ->
            runCatching { ParcelFileDescriptor.fromFd(rawFd) }.getOrElse {
                Log.w(TAG, "fromFd($rawFd) fallo: ${it.message} — bypass nativo sin fd")
                null
            }
        }

        // Inicializa triple buffer lock-free
        ringBuffer = TripleBufferS32(
            capacityFrames = RING_CAPACITY_FRAMES,
            channels = CHANNELS
        )

        Log.i(TAG, "USB OTG Directo establecido: ${targetDevice.deviceName} " +
            "@ ${negotiatedSampleRate}Hz S32_LE (endpoint maxPacket=${ep.maxPacketSize}B, " +
            "bInterval=${ep.interval}); feedback UAC=" +
            (feedbackEndpoint?.let { "SI (addr=0x${it.address.toString(16)}, mps=${it.maxPacketSize}B)" }
                ?: "no (el DAC no lo expone — sincronizacion fina no disponible)"))
        return true
    }

    /**
     * Deriva la mayor SR estandar que cabe en el endpoint isocrono:
     *
     *   framesPorPaquete = maxPacketSize / FRAME_SIZE_BYTES
     *   paquetesPorSegundo = 1000 / (2^(bInterval-1))   (FS: bInterval en ms)
     *
     * En high-speed (UAC2) bInterval N significa 2^(N-1) microtramas de
     * 125us; en full-speed (UAC1) significa N tramas de 1ms. La API de
     * Android no expone la velocidad del bus directamente en UsbEndpoint,
     * asi que se usa la heuristica documentada: maxPacketSize > 1023 implica
     * high-speed (FS no puede superar 1023B por paquete isocrono).
     *
     * Devuelve 0 si ninguna tasa estandar cabe — el llamador aborta con
     * telemetria en vez de configurar una SR imposible.
     */
    private fun negotiateSampleRate(ep: UsbEndpoint): Int {
        val framesPerPacket = ep.maxPacketSize / FRAME_SIZE_BYTES
        if (framesPerPacket <= 0) return 0
        val highSpeed = ep.maxPacketSize > 1023
        val intervalMs = if (highSpeed) {
            // 2^(bInterval-1) microtramas de 0.125ms
            (1 shl (ep.interval.coerceIn(1, 4) - 1)) * 0.125f
        } else {
            ep.interval.coerceIn(1, 8).toFloat()
        }
        val packetsPerSecond = 1000f / intervalMs
        val maxSr = (framesPerPacket * packetsPerSecond).toInt()
            .coerceAtMost(MAX_SAMPLE_RATE)
        val chosen = STANDARD_SAMPLE_RATES.firstOrNull { it <= maxSr } ?: 0
        if (chosen == 0) {
            Log.w(TAG, "Presupuesto del endpoint: ~${maxSr}Hz < ${MIN_SAMPLE_RATE}Hz " +
                "(frames/paquete=$framesPerPacket, intervalo=${intervalMs}ms, " +
                "highSpeed=$highSpeed)")
        } else if (chosen < MAX_SAMPLE_RATE) {
            Log.i(TAG, "SR negociada por endpoint: ${chosen}Hz " +
                "(techo fisico ~${maxSr}Hz, highSpeed=$highSpeed)")
        }
        return chosen
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
                sampleRate    = negotiatedSampleRate,
                channels      = CHANNELS,
                bitDepth      = BIT_DEPTH,
                // Sentinelas (-1 / 0) si el DAC no expone feedback: el lado
                // nativo los interpreta como "sin feedback" y no cambia su
                // comportamiento actual (mismo fpp fijo de siempre).
                feedbackEpAddress     = feedbackEndpoint?.address ?: -1,
                feedbackMaxPacketSize = feedbackEndpoint?.maxPacketSize ?: 0,
                feedbackInterval      = feedbackEndpoint?.interval?.coerceAtLeast(1) ?: 1
            )
        }

        // Delega al hilo nativo via JNI; el hilo URB entrega al DAC en modo
        // asíncrono (el DAC marca el reloj, nosotros seguimos su cadencia).
        // Guard de enlace: si libivanna_omega no cargo (perfil sin root, ABI
        // no cubierta), el external fun lanza UnsatisfiedLinkError y antes
        // tumbaba al llamador (openDirectPath). Ahora degrada con telemetria
        // honesta: la ruta USB existe pero el motor isocrono NO — el audio
        // sigue por el triple buffer/AudioTrack, y isIsochronous() lo dice.
        try {
            nativeStartAsyncEngine(nativeHandle, fileDescriptor?.fd ?: -1)
        } catch (t: Throwable) {
            Log.w(TAG, "Motor nativo isocrono NO disponible (${t.javaClass.simpleName}: " +
                "${t.message}) — la ruta directa queda sin consumidor URB; " +
                "el audio NO esta yendo al DAC por bypass")
            isStreaming.set(false)
            isAsyncSlave.set(false)
            return false
        }

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
     * USB OTG completo.
     *
     * NOTA DE ESTADO (verificado 2026-09-08 leyendo usb_audio_pro_manager.cpp):
     * el motor nativo YA es real, no un stub — URBs isocronos via
     * USBDEVFS_SUBMITURB/REAPURBNDELAY con fallback a write() con pacing,
     * anillo SPSC y parada limpia con DISCARDURB. El comentario anterior
     * ("stub de logging") quedo obsoleto y mentia en la telemetria.
     *
     * El bloque se acota a la capacidad del triple buffer (RING_CAPACITY_FRAMES):
     * antes solo se acotaba al tamano del array de entrada, y un bloque mayor
     * que el buffer lanzaba BufferOverflowException en el hilo de audio.
     */
    fun writeAudio(samples: FloatArray, frameCount: Int) {
        if (!isStreaming.get() || !::ringBuffer.isInitialized) return
        val n = frameCount
            .coerceAtMost(samples.size / CHANNELS)
            .coerceAtMost(RING_CAPACITY_FRAMES)
        if (n <= 0) return
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

    /**
     * Estado del feedback UAC: [hasFeedback(0/1), paquetesRecibidos,
     * ultimoValorCrudoLE, direccionEndpoint]. Sin DAC con feedback (o motor
     * no arrancado), hasFeedback=0 — no es un error, es el caso normal para
     * la mayoria de los DAC UAC1 y varios UAC2 simples.
     */
    fun feedbackInfo(): IntArray = try {
        nativeGetFeedbackInfo() ?: IntArray(4)
    } catch (t: Throwable) { IntArray(4) }

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
        feedbackEndpoint = null
        audioInterface = null
        fileDescriptor = null
        // Sin este reset, el id quedaba fantasma: una reconexion del mismo
        // DAC con deviceId reasignado por el kernel podia no reconocerse
        // como 'nuestro' en el siguiente ciclo de attach/detach.
        openDeviceId = -1
    }

    /**
     * Triple buffer lock-free S32_LE para evitar pausas del GC.
     *
     * Patron clasico de tres indices:
     *   - writeIndex: el que el productor esta llenando AHORA
     *   - readyIndex: el ultimo completo, listo para el consumidor
     *   - readIndex:  el que el consumidor esta leyendo AHORA
     * commitWrite() intercambia write<->ready (publica el bloque);
     * swapRead() intercambia read<->ready (el consumidor toma el listo).
     *
     * ESTADO REAL (verificado 2026-09-08): el consumidor de audio NO lee de
     * este buffer — el camino vivo es nativeWriteFrames() hacia el anillo
     * SPSC nativo de usb_audio_pro_manager.cpp. getReadBuffer()/swapRead()
     * quedan como API correcta para un futuro consumidor Java (p.ej. un
     * visualizador de onda), ya con la semantica arreglada: antes swapRead()
     * intercambiaba writeIndex<->readIndex, y un consumidor que lo usara
     * habria leido el buffer A MEDIO ESCRIBIR por el productor.
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
            // read <-> ready: el consumidor toma el ultimo bloque publicado.
            // (Antes intercambiaba write<->read: semantica rota — el
            // consumidor habria leido el buffer que el productor esta
            // escribiendo en este instante.)
            val oldReady = readyIndex
            readyIndex = readIndex
            readIndex = oldReady
        }
    }

    // JNI native methods
    private external fun nativeStartAsyncEngine(handle: Long, fd: Int)
    private external fun nativeStopAsyncEngine(handle: Long)

    // Motor asíncrono REAL (URBs isócronos usbfs) — cableado de punta a punta
    private external fun nativeConfigureEndpoint(
        epAddress: Int, maxPacketSize: Int, interval: Int,
        sampleRate: Int, channels: Int, bitDepth: Int,
        feedbackEpAddress: Int, feedbackMaxPacketSize: Int, feedbackInterval: Int
    )
    private external fun nativeWriteFrames(samples: FloatArray, frames: Int): Int
    private external fun nativeGetEngineStats(): IntArray?
    private external fun nativeGetFeedbackInfo(): IntArray?
    private external fun nativeIsIsochronous(): Boolean
}
