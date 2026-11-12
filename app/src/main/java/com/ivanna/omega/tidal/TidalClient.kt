package com.ivanna.omega.tidal

import android.content.Context
import com.ivanna.omega.audio.IvannaBridgePlayer

/**
 * TIDAL — cliente completo (v2.5.417).
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
        private const val KEY_QUALITY = "quality"
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
    val resolver = TidalStreamResolver(http)
    val controller = TidalPlayerController(
        BridgePlayerSink(player),
        api,
        auth,
        resolver,
        appContext.cacheDir
    )

    private val prefs = appContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    var quality: String
        get() = prefs.getString(KEY_QUALITY, TidalApiClient.DEFAULT_QUALITY)
            ?: TidalApiClient.DEFAULT_QUALITY
        set(value) {
            val q = when (value) {
                TidalApiClient.QUALITY_LOW, TidalApiClient.QUALITY_HIGH,
                TidalApiClient.QUALITY_LOSSLESS, TidalApiClient.QUALITY_HI_RES -> value
                else -> TidalApiClient.DEFAULT_QUALITY
            }
            prefs.edit().putString(KEY_QUALITY, q).apply()
            controller.quality = q
        }

    init {
        controller.quality = quality
        // Limpieza de temporales DASH de ejecuciones anteriores.
        runCatching {
            appContext.cacheDir.listFiles()
                ?.filter { it.name.startsWith("tidal_dash_") && it.name.endsWith(".m4s") }
                ?.forEach { it.delete() }
        }
    }

    /** Cierra el controlador (su scope). Sin orden respecto a player.release(). */
    fun close() {
        controller.close()
    }
}
