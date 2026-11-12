package com.ivanna.omega.tidal

import android.content.Context
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import com.ivanna.omega.audio.IvannaBridgePlayer
import java.io.File

/**
 * TIDAL — cliente completo (v2.5.418).
 *
 * Un solo objeto creado UNA vez en el ámbito del setContent de MainActivity
 * (vida de la actividad) y pasado a la pantalla TIDAL. Así la música sigue
 * sonando al cambiar de pestaña (CONTROL ↔ TIDAL) o al ir a otras pantallas:
 * el reproductor y el controlador no mueren con la composición de la pestaña.
 *
 * La calidad pedida (LOSSLESS por defecto, MAX = HI_RES_LOSSLESS) persiste
 * en prefs planas "ivanna_tidal_prefs": no es un dato sensible.
 */
class TidalClient(context: Context, player: IvannaBridgePlayer) {

    companion object {
        private const val PREFS_NAME = "ivanna_tidal_prefs"
        private const val KEY_QUALITY_PREF = "quality_pref"
        private const val KEY_QUALITY = "quality"
        /** Calidad automática: WiFi → HI_RES, móvil → LOSSLESS. */
        const val QUALITY_AUTO = "AUTO"
    }

    private val appContext = context.applicationContext

    val store: TidalTokenStore = TidalSessionStore(appContext)
    val http: TidalHttp = UrlConnectionTidalHttp()
    // v2.5.417: la API v1 exige countryCode (+ sessionId) en toda petición;
    // se leen en vivo del store (tras el login trae MX; antes, vacíos).
    val api = TidalApiClient(
        http,
        countryCodeProvider = { store.load()?.countryCode ?: "" },
        sessionIdProvider = { store.load()?.sessionId ?: "" }
    )
    val auth = TidalAuth(http, store)
    /** Caché acotada de streams (v2.5.421): repetición instantánea. */
    val streamCache = TidalStreamCache(File(appContext.cacheDir, "tidal_cache"))
    val resolver = TidalStreamResolver(http, streamCache)
    val controller = TidalPlayerController(
        BridgePlayerSink(player),
        api,
        auth,
        resolver,
        appContext.cacheDir
    )

    private val prefs = appContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    /**
     * Preferencia de calidad: AUTO (por defecto) o una fija. Migra la
     * preferencia vieja ("quality") si existe: la elección de Esse se
     * respeta, no se pierde con el cambio.
     */
    var qualityPref: String
        get() = prefs.getString(KEY_QUALITY_PREF, null)
            ?: prefs.getString(KEY_QUALITY, null)
            ?: QUALITY_AUTO
        set(value) {
            val q = when (value) {
                QUALITY_AUTO, TidalApiClient.QUALITY_LOW, TidalApiClient.QUALITY_HIGH,
                TidalApiClient.QUALITY_LOSSLESS, TidalApiClient.QUALITY_HI_RES -> value
                else -> QUALITY_AUTO
            }
            prefs.edit().putString(KEY_QUALITY_PREF, q).apply()
            controller.quality = resolvedQuality()
        }

    /** Calidad que se pide al servidor (v2.5.421): la preferencia, o AUTO
     * resuelta por la red. El controlador siempre recibe la resuelta. */
    var quality: String
        get() = resolvedQuality()
        set(value) {
            qualityPref = value
        }

    /**
     * Resuelve AUTO por la red actual: WiFi (o red sin contador) →
     * HI_RES_LOSSLESS (FLAC hasta 24-bit/192 kHz); móvil con contador →
     * LOSSLESS (FLAC 16-bit/44.1 kHz, sin gastar el plan de datos).
     * Una preferencia explícita nunca se resuelve: se respeta tal cual.
     */
    fun resolvedQuality(): String {
        if (qualityPref != QUALITY_AUTO) return qualityPref
        return if (isUnmeteredNetwork()) TidalApiClient.QUALITY_HI_RES
        else TidalApiClient.QUALITY_LOSSLESS
    }

    private fun isUnmeteredNetwork(): Boolean {
        val cm = appContext.getSystemService(Context.CONNECTIVITY_SERVICE) as? ConnectivityManager
            ?: return false
        val caps = cm.getNetworkCapabilities(cm.activeNetwork) ?: return false
        return caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_METERED)
    }

    init {
        controller.quality = quality
        // Limpieza de temporales DASH/BTS de ejecuciones anteriores
        // (los de la caché viven en tidal_cache/ y NO se tocan).
        runCatching {
            appContext.cacheDir.listFiles()
                ?.filter {
                    (it.name.startsWith("tidal_dash_") && it.name.endsWith(".m4s")) ||
                        it.name.startsWith("tidal_bts_")
                }
                ?.forEach { it.delete() }
        }
    }

    /** Cierra el controlador (su scope). Sin orden respecto a player.release(). */
    fun close() {
        controller.close()
    }
}
