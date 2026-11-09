package com.ivanna.omega.security

import android.content.Context
import android.content.pm.PackageManager
import android.provider.Settings
import android.util.Log
import androidx.security.crypto.EncryptedSharedPreferences
import androidx.security.crypto.MasterKey
import com.ivanna.omega.BuildConfig
import com.ivanna.omega.core.NativeLibraryLoader
import java.io.File
import java.security.MessageDigest

/**
 * SecStrings — literales de seguridad cifrados (fase 2).
 *
 * Los literales sensibles de IntegrityGate (nombre del archivo de prefs,
 * claves, tag de log, nombre del asset de hashes) NO van en claro en el
 * dex: van cifrados con XOR de un byte y se descifran en memoria al usarlos.
 * Esto no es seguridad real contra un atacante que lee el código (la clave
 * está en el mismo dex), pero quita los literales de la tabla de strings
 * del dex y obliga a analizar el flujo, no a buscar por texto. Los literales
 * de seguridad nativos (rutas, tag de log) van cifrados en C++ (XorString).
 *
 * Generado con tools/security/obfuscate_strings.py (regenerar si cambian
 * los literales). La clave XOR (0x5A) es deliberadamente simple: el objetivo
 * es quitar los literales de la tabla de strings, no cifrado fuerte.
 */
internal object SecStrings {
    private const val KEY: Int = 0x5A

    private fun dec(bytes: ByteArray): String {
        val out = ByteArray(bytes.size)
        for (i in bytes.indices) out[i] = (bytes[i].toInt() xor KEY).toByte()
        return String(out, Charsets.UTF_8)
    }

    // "ivanna_integrity"
    val prefsFile: String
        get() = dec(byteArrayOf(51, 44, 59, 52, 52, 59, 5, 51, 52, 46, 63, 61, 40, 51, 46, 35))
    // "android_id"
    val keyAndroidId: String
        get() = dec(byteArrayOf(59, 52, 62, 40, 53, 51, 62, 5, 51, 62))
    // "version_code"
    val keyVersionCode: String
        get() = dec(byteArrayOf(44, 63, 40, 41, 51, 53, 52, 5, 57, 53, 62, 63))
    // "IntegrityGate"
    val tag: String
        get() = dec(byteArrayOf(19, 52, 46, 63, 61, 40, 51, 46, 35, 29, 59, 46, 63))
    // "ivanna_so_hashes.txt"
    val soHashesAsset: String
        get() = dec(byteArrayOf(51, 44, 59, 52, 52, 59, 5, 41, 53, 5, 50, 59, 41, 50, 63, 41, 116, 46, 34, 46))
}

/**
 * IntegrityGate — Puerta de integridad anti-piratería (Kotlin).
 *
 * CONFIDENCIAL (interno): los detalles de este módulo NO se documentan en el
 * README público ni en material promocional. Ver docs/SECURITY_INTERNAL.md.
 *
 * Qué verifica (todo on-device, sin servidor):
 *  1. Pin de certificado de firma: el certificado con el que se firmó ESTE
 *     APK debe coincidir con el pin generado en build-time desde el keystore
 *     que firmó ese build (tools/security/generate_build_pins.py). Un APK
 *     re-firmado tiene otro certificado -> MISMATCH -> bloqueo.
 *  2. Vinculación a la instalación: ANDROID_ID (que en Android 8+ depende
 *     del dispositivo Y de la clave de firma) queda registrado en el primer
 *     arranque (cifrado con EncryptedSharedPreferences). Si cambia sin que
 *     haya subido el versionCode (re-firma o clonación en el mismo build),
 *     se bloquea. Si sube el versionCode (actualización legítima, que con
 *     keystore efímero puede cambiar la clave), se re-vincula.
 *  3. Self-check nativo: la .so debe venir de una ruta legítima
 *     (ver ivanna_integrity.cpp).
 *  4. Hash de la .so en runtime (fase 2): el SHA-256 de la .so instalada
 *     debe coincidir con el hash generado en build-time y guardado en el
 *     asset ivanna_so_hashes.txt (tarea generateSoHashes). Detecta la
 *     sustitución de la .so por una copia modificada. Si el asset falta
 *     (build sin generateSoHashes), se avisa y se continúa (el pin de
 *     certificado sigue protegiendo); en release sin asset se bloquea.
 *  5. Anti-tamper (fase 2): depurador, Frida, framework de hooks o
 *     emulador en el proceso app -> bloqueo. Root por sí solo NO bloquea
 *     (el producto está diseñado para dispositivos rooteados).
 *
 * Fail-closed: si cualquier comprobación falla, [isLocked] = true y el DSP
 * no se inicializa (las guardas en DSPBridge/MainActivity/AudioEngine lo
 * impiden) -> el audio pasa en limpio, el teléfono sigue funcionando.
 * Recuperación legítima: borrar los datos de la app vuelve a vincular
 * (primer arranque) — útil tras un restablecimiento de fábrica.
 *
 * Se ejecuta de forma síncrona en IVANNAApplication.onCreate(), antes de
 * cualquier init DSP. Debe mantenerse rápido (<200 ms): cert + prefs +
 * dladdr son baratos; no se hashea el .so completo en v1.
 */
object IntegrityGate {
    // Los literales van cifrados (SecStrings, fase 2): no aparecen en claro
    // en la tabla de strings del dex.
    private val TAG: String get() = SecStrings.tag
    private val PREFS_FILE: String get() = SecStrings.prefsFile
    private val KEY_ANDROID_ID: String get() = SecStrings.keyAndroidId
    private val KEY_VERSION_CODE: String get() = SecStrings.keyVersionCode

    // Bitmask de señales anti-tamper (ivanna_antitamper.h). Deben coincidir
    // con los valores nativos; si cambian en C++, cambiar aquí también.
    private const val SIGNAL_DEBUGGER = 0x01
    private const val SIGNAL_FRIDA = 0x02
    private const val SIGNAL_XPOSED = 0x04
    private const val SIGNAL_EMULATOR = 0x08
    private const val SIGNAL_ROOT = 0x10
    // Señales que disparan lockdown en release (root NO: el producto está
    // diseñado para dispositivos rooteados; el daemon corre como root).
    private const val LOCKDOWN_MASK = SIGNAL_DEBUGGER or SIGNAL_FRIDA or SIGNAL_XPOSED or SIGNAL_EMULATOR

    // 0 = OK, 1 = MISMATCH, 2 = NO_PIN (sin pin generado).
    private const val CERT_OK = 0
    private const val CERT_MISMATCH = 1
    private const val CERT_NO_PIN = 2

    @Volatile
    var isLocked: Boolean = false
        private set

    @Volatile
    private var verified: Boolean = false

    fun isVerified(): Boolean = verified && !isLocked

    /**
     * Verifica la integridad. Retorna true si todo está en orden.
     * Idempotente: una vez verificado con éxito no se repite el trabajo.
     * Si falla, deja [isLocked] = true (fail-closed).
     */
    @Synchronized
    fun verify(context: Context): Boolean {
        if (verified && !isLocked) return true
        if (isLocked) return false

        // 0. La lib nativa debe estar cargada para llamar a los métodos JNI.
        // Sin ella el DSP no puede correr de todos modos (isLoaded=false).
        val libOk = runCatching { NativeLibraryLoader.ensureLoaded() }.getOrDefault(false)
        if (!libOk) {
            Log.w(TAG, "Lib nativa no cargada: DSP no disponible (bloqueo fail-closed)")
            lock()
            return false
        }

        // 1. Pin de certificado de firma.
        val certHex = signingCertSha256Hex(context)
        if (certHex == null) {
            Log.w(TAG, "No se pudo leer el certificado de firma")
            lock()
            return false
        }
        when (nativeVerifyCert(certHex)) {
            CERT_OK -> { /* sigue */ }
            CERT_NO_PIN -> {
                if (BuildConfig.DEBUG) {
                    Log.w(TAG, "Sin pin de build (debug): verificación de cert omitida")
                } else {
                    Log.w(TAG, "Sin pin de build en release: bloqueo fail-closed")
                    lock()
                    return false
                }
            }
            else -> {
                Log.w(TAG, "Certificado de firma NO coincide con el pin de build (re-firma)")
                lock()
                return false
            }
        }

        // 2. Vinculación ANDROID_ID (con re-vinculación en actualización).
        if (!checkBinding(context)) {
            Log.w(TAG, "Vinculación de instalación no válida (re-firma o clonación)")
            lock()
            return false
        }

        // 3. Self-check nativo (ruta de carga de la .so).
        val selfOk = runCatching { nativeSelfCheck() }.getOrDefault(false)
        if (!selfOk) {
            Log.w(TAG, "Self-check nativo falló (ruta de carga sospechosa)")
            lock()
            return false
        }

        // 4. Hash de la .so en runtime vs hash generado en build-time (fase 2).
        if (!checkSoHash(context)) {
            Log.w(TAG, "Hash de la .so no coincide con el hash de build (sustitución)")
            lock()
            return false
        }

        // 5. Anti-tamper (fase 2): depurador / Frida / hooks / emulador.
        val signals = runCatching { nativeAntiTamperSignals() }.getOrDefault(0)
        if (signals and LOCKDOWN_MASK != 0) {
            Log.w(TAG, "Anti-tamper: señales de manipulación (mask=0x${signals.toString(16)})")
            lock()
            return false
        }
        if (signals and SIGNAL_ROOT != 0) {
            // Solo informativo: el producto corre en dispositivos rooteados
            // (módulo Magisk + daemon como root). No bloquea.
            Log.i(TAG, "Anti-tamper: root detectado (informativo, no bloquea)")
        }

        nativeSetGate(true)
        verified = true
        Log.i(TAG, "Integridad verificada (cert + vinculación + self-check + hash .so + anti-tamper)")
        return true
    }

    /** Bloqueo manual (p. ej. desde UI si se detecta manipulación). */
    fun lockdown() {
        lock()
    }

    private fun lock() {
        isLocked = true
        verified = false
        runCatching { nativeLockdown() }
        Log.w(TAG, "LOCKDOWN: DSP bloqueado (fail-closed, audio en passthrough)")
    }

    private fun signingCertSha256Hex(context: Context): String? {
        return runCatching {
            val pm = context.packageManager
            val info = pm.getPackageInfo(
                context.packageName,
                PackageManager.GET_SIGNING_CERTIFICATES
            )
            val signers = info.signingInfo?.apkContentsSigners
            if (signers.isNullOrEmpty()) return null
            val digest = MessageDigest.getInstance("SHA-256")
            // Con un solo firmante es el habitual; con varios se usa el primero
            // (el pin de build se genera del firmante principal del keystore).
            // OJO: "%02x".format(byte) directo es un BUG — un byte >= 0x80
            // (p. ej. 0xFF = -1) se extiende con signo y da "ffffffff"
            // (8 chars) en vez de "ff". Se enmascara a 0xFF antes de formatear.
            val bytes = digest.digest(signers[0].toByteArray())
            bytes.joinToString("") { "%02x".format(it.toInt() and 0xFF) }
        }.getOrNull()
    }

    private fun checkBinding(context: Context): Boolean {
        val androidId = Settings.Secure.getString(
            context.contentResolver, Settings.Secure.ANDROID_ID
        ) ?: run {
            // ANDROID_ID nulo (raro): no se puede vincular; no se bloquea
            // para evitar falsos positivos. El pin de cert sigue protegiendo.
            Log.w(TAG, "ANDROID_ID nulo: vinculación omitida")
            return true
        }
        val versionCode = currentVersionCode(context)
        return runCatching {
            val masterKey = MasterKey.Builder(context)
                .setKeyScheme(MasterKey.KeyScheme.AES256_GCM)
                .build()
            val prefs = EncryptedSharedPreferences.create(
                context,
                PREFS_FILE,
                masterKey,
                EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV,
                EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM
            )
            val storedId = prefs.getString(KEY_ANDROID_ID, null)
            val storedVersion = prefs.getLong(KEY_VERSION_CODE, -1L)
            if (storedId == null) {
                // Primer arranque: vincular.
                prefs.edit()
                    .putString(KEY_ANDROID_ID, androidId)
                    .putLong(KEY_VERSION_CODE, versionCode)
                    .apply()
                Log.i(TAG, "Vinculación inicial registrada")
                return true
            }
            if (storedId == androidId) {
                // Mismo dispositivo/instalación: actualizar versionCode si subió.
                if (versionCode > storedVersion) {
                    prefs.edit().putLong(KEY_VERSION_CODE, versionCode).apply()
                }
                return true
            }
            // ANDROID_ID cambió. Si es una actualización (versionCode mayor),
            // la clave de firma pudo cambiar (keystore efímero) -> re-vincular.
            // Si es el mismo build o inferior -> re-firma o clonación.
            if (versionCode > storedVersion) {
                prefs.edit()
                    .putString(KEY_ANDROID_ID, androidId)
                    .putLong(KEY_VERSION_CODE, versionCode)
                    .apply()
                Log.i(TAG, "Re-vinculación por actualización (versionCode $storedVersion -> $versionCode)")
                return true
            }
            false
        }.getOrDefault(false)
    }

    /**
     * Paso 4 (fase 2): hash de la .so instalada vs hash generado en build-time.
     *
     * El asset ivanna_so_hashes.txt (generado por la tarea generateSoHashes)
     * tiene una línea por .so: "libivanna_omega.so <sha256-hex>". La .so
     * instalada se hashea con el SHA-256 nativo (ivanna_integrity.cpp) y se
     * compara en tiempo constante. Si el asset falta: en debug se omite (el
     * build local no siempre genera el asset), en release se bloquea
     * (el build de release debe generar el asset; la CI lo verifica).
     */
    private fun checkSoHash(context: Context): Boolean {
        val expected = runCatching {
            context.assets.open(SecStrings.soHashesAsset).bufferedReader().use { it.readText() }
        }.getOrNull()
        if (expected.isNullOrEmpty()) {
            if (BuildConfig.DEBUG) {
                Log.w(TAG, "Sin asset de hashes de .so (debug): hash .so omitido")
                return true
            }
            Log.w(TAG, "Sin asset de hashes de .so en release: bloqueo fail-closed")
            return false
        }
        val expectedHash = expected.lineSequence()
            .map { it.trim() }
            .filter { it.isNotEmpty() }
            .firstOrNull { it.startsWith("libivanna_omega.so") }
            ?.split(Regex("\\s+"))?.getOrNull(1)
        if (expectedHash.isNullOrEmpty()) {
            Log.w(TAG, "Asset de hashes sin entrada para libivanna_omega.so")
            return false
        }
        val actual = hashInstalledSo(context)
        if (actual.isNullOrEmpty()) {
            Log.w(TAG, "No se pudo hashear la .so instalada")
            return false
        }
        if (actual.length != expectedHash.length) return false
        var diff = 0
        for (i in actual.indices) diff = diff or (actual[i].code xor expectedHash[i].code)
        return diff == 0
    }

    /**
     * SHA-256 de la libivanna_omega.so instalada.
     *
     * FIX (2026-11-09, falso positivo): con useLegacyPackaging=false la .so
     * NO se extrae a nativeLibraryDir — se carga directamente desde el APK
     * (base.apk) y ese directorio queda vacío. El código anterior hasheaba
     * solo nativeLibraryDir/libivanna_omega.so, que no existe -> null ->
     * bloqueo fail-closed en TODA instalación release (verificado en el
     * Moto G85: lib/arm64/ vacío, sin libivanna_omega.so en /data/app).
     * Ahora: si no está extraída, se hashea la entrada
     * lib/<abi>/libivanna_omega.so del APK (sourceDir) en streaming.
     * La comparación sigue siendo contra el hash de build (asset); la
     * protección (detectar sustitución de la .so) queda intacta.
     */
    private fun hashInstalledSo(context: Context): String? {
        val extracted = File(context.applicationInfo.nativeLibraryDir, "libivanna_omega.so")
        if (extracted.isFile) {
            runCatching { nativeSha256File(extracted.absolutePath) }.getOrNull()?.let { return it }
        }
        return runCatching {
            val apk = File(context.applicationInfo.sourceDir)
            val digest = MessageDigest.getInstance("SHA-256")
            java.util.zip.ZipFile(apk).use { zip ->
                val entry = android.os.Build.SUPPORTED_ABIS
                    .asSequence()
                    .map { abi -> zip.getEntry("lib/$abi/libivanna_omega.so") }
                    .firstOrNull { it != null }
                    ?: return@runCatching null
                zip.getInputStream(entry).use { input ->
                    val buf = ByteArray(65536)
                    var n: Int
                    while (input.read(buf).also { n = it } > 0) digest.update(buf, 0, n)
                }
            }
            digest.digest().joinToString("") { "%02x".format(it.toInt() and 0xFF) }
        }.getOrNull()
    }

    private fun currentVersionCode(context: Context): Long {
        return runCatching {
            val info = context.packageManager.getPackageInfo(context.packageName, 0)
            @Suppress("DEPRECATION")
            if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.P) {
                info.longVersionCode
            } else {
                @Suppress("DEPRECATION")
                info.versionCode.toLong()
            }
        }.getOrDefault(0L)
    }

    // ── JNI (ivanna_integrity_jni.cpp, libivanna_omega.so) ─────────────────
    private external fun nativeVerifyCert(certSha256Hex: String): Int
    private external fun nativeSetGate(ok: Boolean)
    private external fun nativeIsGateOk(): Boolean
    private external fun nativeSelfCheck(): Boolean
    private external fun nativeLockdown()
    // Fase 2: SHA-256 hex de un archivo (la .so instalada); null si no se
    // puede abrir o leer. Bitmask de señales anti-tamper (ivanna_antitamper.h).
    private external fun nativeSha256File(path: String): String?
    private external fun nativeAntiTamperSignals(): Int
}
