package com.ivanna.omega.audio

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.os.Build
import android.util.Log

/**
 * UsbDacAttachReceiver — despierta el path de "entrada tipo C / ruta libre
 * para DAC" cuando el usuario conecta un DAC USB DESPUES de arrancar la app.
 *
 * POR QUE EXISTE (verificado antes de escribirlo):
 *   UsbAudioProManager tenia el path OTG directo completo (claimInterface,
 *   endpoint isoc, motor nativo de URBs) pero NADIE lo llamaba: ni receiver
 *   de manifest, ni receiver dinamico, ni sondeo — grep de requestDirectAccess
 *   fuera de su propio archivo = 0 hits. Conectar un DAC a mitad de sesion
 *   era invisible para la ruta directa; el usuario tenia que reiniciar la app
 *   y aun asi nadie arrancaba el motor.
 *
 * ALCANCE:
 *   Solo ATTACHED (es lo unico que el sistema entrega a receivers de
 *   manifest). DETACHED se cubre con el receiver dinamico que
 *   UsbAudioProManager registra en startHotplugMonitor() — este receiver
 *   ademas lo arranca, asi la primera conexion deja el monitor vivo.
 *
 *   Este receiver NO abre el dispositivo ni pide permiso: solo notifica a
 *   UsbAudioProManager, que es quien decide (tiene el contexto de app, el
 *   UsbManager y la maquina de estados del motor). Asi el receiver es
 *   trivial, rapido (broadcasts de sistema tienen limite de ~10s) y sin
 *   estado propio que sincronizar.
 */
class UsbDacAttachReceiver : BroadcastReceiver() {

    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action != UsbManager.ACTION_USB_DEVICE_ATTACHED) return

        @Suppress("DEPRECATION")
        val device: UsbDevice? = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
        } else {
            intent.getParcelableExtra(UsbManager.EXTRA_DEVICE)
        }
        if (device == null) {
            Log.w(TAG, "ATTACHED sin EXTRA_DEVICE — broadcast malformado, ignorado")
            return
        }

        Log.i(TAG, "DAC USB conectado: ${device.productName ?: device.deviceName} " +
            "(vid=${device.vendorId.toString(16)} pid=${device.productId.toString(16)})")

        // Delega en el manager: el decide si ya hay sesion, si hay permiso,
        // y arranca el monitor dinamico de hotplug si aun no estaba vivo.
        // runCatching: un fallo aqui NUNCA debe tumbar el broadcast del
        // sistema (el receiver corre en el main thread del proceso).
        runCatching {
            UsbAudioProManager.getInstance(context).onDeviceAttached(device)
        }.onFailure { Log.w(TAG, "onDeviceAttached: ${it.message}") }
    }

    companion object {
        private const val TAG = "UsbDacAttachReceiver"
    }
}
