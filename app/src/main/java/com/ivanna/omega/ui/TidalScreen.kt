package com.ivanna.omega.ui

import android.annotation.SuppressLint
import android.webkit.WebResourceRequest
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import com.ivanna.omega.audio.IvannaBridgePlayer
import com.ivanna.omega.tidal.TidalAlbum
import com.ivanna.omega.tidal.TidalApiClient
import com.ivanna.omega.tidal.TidalArtist
import com.ivanna.omega.tidal.TidalAuth
import com.ivanna.omega.tidal.TidalClient
import com.ivanna.omega.tidal.TidalPlaylist
import com.ivanna.omega.tidal.TidalSearchResult
import com.ivanna.omega.tidal.TidalTrack
import com.ivanna.omega.ui.theme.AuroraCyan
import com.ivanna.omega.ui.theme.MagistralGold
import com.ivanna.omega.ui.theme.MagistralGoldHi
import com.ivanna.omega.ui.theme.MagistralLineStrong
import com.ivanna.omega.ui.theme.MagistralMuted
import com.ivanna.omega.ui.theme.MagistralPanel
import com.ivanna.omega.ui.theme.MagistralSerif
import com.ivanna.omega.ui.theme.MagistralText
import com.ivanna.omega.ui.theme.MagistralVoid
import com.ivanna.omega.ui.theme.TextMuted
import com.ivanna.omega.ui.theme.TextPrimary
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * TIDAL dentro de IVANNA (v2.5.416) — pantalla Compose, paleta Aurora Obsidiana.
 *
 * - Login SOLO en WebView con la página de Tidal: Esse escribe SUS
 *   credenciales ahí; la app solo recibe el `code` del redirect y lo
 *   intercambia por tokens (PKCE, sin secreto). Fallback manual: pegar la
 *   URL de redirect a mano.
 * - Búsqueda (canciones/álbumes/artistas/listas), mis listas, álbum de un
 *   artista, temas de álbum/lista. La cola son IDs de Tidal; la URL se
 *   resuelve fresca al reproducir cada tema.
 * - La reproducción va por el BridgePlayer (cadena magistral al 100%):
 *   NADA de WebView para sonar, NADA de captura, NADA del reproductor del
 *   sistema. La pantalla muestra la calidad ENTREGADA real (si el servidor
 *   degrada a AAC, se ve).
 * - Fail-closed: cualquier error de Tidal se muestra en claro y el resto de
 *   la app sigue funcionando.
 *
 * Refinamiento magistral v2.5.442: fichas del prototipo aprobado (Pantalla
 * VIII · Tidal, módulos R/S/T) con texto exacto; dorado con jerarquía sobre
 * fondo oscuro profundo; cian solo donde se toca o donde algo está vivo.
 */
private val tidalScreenSpec = SpecInfo(
    title = "Tidal",
    what = "El entorno de la fuente principal: cómo IVANNA pide el audio a Tidal " +
        "(calidad), cómo lo adelanta (prefetch y caché) y cómo lo reproduce " +
        "(BridgePlayer, su reproductor propio).",
    purpose = "La cadena más magistral del mundo no procesa lo que no llega: este " +
        "entorno garantiza que el audio llegue completo, a tiempo y a la mejor " +
        "calidad que tu red permite, y que suene al 100 % por la cadena interna.",
    whenUse = "Configúralo una vez (AUTO + prefetch + BridgePlayer) y olvídalo: es la " +
        "combinación de referencia para juzgar cualquier ajuste de los demás entornos.",
    whenAvoid = "No pelees contra tu red: con datos móviles ahorcados, forzar Hi-Res " +
        "produce cortes, no calidad. La calidad se decide en la descarga; el DSP no " +
        "puede inventar lo que la red no entregó."
)

/** R · Calidad de streaming (texto exacto del prototipo). */
private val tidalQualitySpec = SpecInfo(
    title = "Calidad de streaming",
    what = "El selector de calidad de origen. AUTO negocia según la red: WiFi pide " +
        "HI_RES (FLAC hasta 192 kHz) y datos móviles piden LOSSLESS (FLAC 44.1/16). " +
        "HI_RES fuerza lo máximo siempre; LOSSLESS fija calidad CD siempre.",
    purpose = "Para equilibrar fidelidad y continuidad: la mejor pista es la que llega " +
        "entera. AUTO existe porque una descarga Hi-Res interrumpida suena " +
        "infinitamente peor que un Lossless perfecto.",
    whenUse = "AUTO, siempre, salvo plan de datos holgado y cobertura excelente. En casa " +
        "con WiFi ya estás en HI_RES sin tocar nada.",
    whenAvoid = "Nunca fuerces HI_RES con datos móviles lentos o medidos: el búfer se " +
        "vacía, llegan los tronidos y la culpa parece del DSP. Tampoco fijes LOSSLESS " +
        "en WiFi por costumbre: regalas la mitad de la cadena Hi-Res."
)

/** S · Prefetch y caché (texto exacto del prototipo). */
private val tidalPrefetchSpec = SpecInfo(
    title = "Prefetch y caché",
    what = "El prefetch descarga las siguientes pistas de la cola mientras suena la " +
        "actual, con un reintento automático. La caché guarda hasta 500 MB / 12 " +
        "archivos con expulsión LRU: sale el archivo que menos escuchas.",
    purpose = "Para que la cadena nunca espere a la red: si la pista ya está en casa, " +
        "no hay tronido de red que valga. Es la diferencia entre «la red falló» y " +
        "«ni me enteré».",
    whenUse = "Siempre activo en WiFi. En datos móviles, activo si tu plan lo permite: " +
        "el prefetch gasta datos en pistas que quizá saltes.",
    whenAvoid = "Con el almacenamiento casi lleno: la caché compite por el mismo espacio " +
        "que tus descargas y tus fotos. Y no lo confundas con descarga offline " +
        "permanente: la caché rota, es un búfer, no tu biblioteca."
)

/** T · BridgePlayer, reproductor propio (texto exacto del prototipo). */
private val tidalBridgePlayerSpec = SpecInfo(
    title = "BridgePlayer · Reproductor propio",
    what = "El reproductor propio de IVANNA: reproduce Tidal y tus archivos locales " +
        "dentro de la app, sin redirigir al reproductor del sistema, pasando el 100 % " +
        "del audio por la cadena magistral.",
    purpose = "Es la única vía donde los efectos son el 100 % de lo que oyes. En captura, " +
        "el original sigue sonando a nivel pleno y la copia procesada llega ~140 ms " +
        "tarde mezclada: por eso en captura todo se siente sutil y con eco de fondo.",
    whenUse = "Siempre que puedas: es la ruta de referencia para juzgar cualquier ajuste " +
        "de esta interfaz. Si un cambio no se nota aquí, no se nota en ningún sitio.",
    whenAvoid = "Para lo que la app no puede reproducir —llamadas, audio del sistema, " +
        "otras apps— BridgePlayer no aplica: eso es territorio de la Ruta B " +
        "(System · O), con sus condiciones."
)

@Composable
fun TidalScreen(
    tidal: TidalClient,
    player: IvannaBridgePlayer,
    onBack: () -> Unit
) {
    val authState by tidal.auth.stateFlow.collectAsState()
    val controller = tidal.controller
    val currentTrack by controller.currentTrack.collectAsState()
    val queue by controller.queue.collectAsState()
    val queueIndex by controller.queueIndex.collectAsState()
    val qualityLabel by controller.qualityLabel.collectAsState()
    val statusMessage by controller.statusMessage.collectAsState()
    val isPlaying by controller.isPlaying.collectAsState()
    val isLoading by controller.isLoading.collectAsState()
    val positionMs by player.currentPositionMs.collectAsState()
    val durationMs by player.durationMs.collectAsState()

    val scope = rememberCoroutineScope()
    var query by remember { mutableStateOf("") }
    var results by remember { mutableStateOf<TidalSearchResult?>(null) }
    var listTitle by remember { mutableStateOf("") }
    var listTracks by remember { mutableStateOf<List<TidalTrack>?>(null) }
    var listAlbums by remember { mutableStateOf<List<TidalAlbum>?>(null) }
    var myPlaylists by remember { mutableStateOf<List<TidalPlaylist>?>(null) }
    var busy by remember { mutableStateOf(false) }
    var error by remember { mutableStateOf<String?>(null) }
    var loginSession by remember { mutableStateOf<TidalAuth.AuthState.Authorizing?>(null) }
    var manualUrl by remember { mutableStateOf("") }
    var loginBusy by remember { mutableStateOf(false) }
    var loginError by remember { mutableStateOf<String?>(null) }
    var quality by remember { mutableStateOf(tidal.qualityPref) }
    var dragMs by remember { mutableStateOf<Long?>(null) }

    // El token (y su posible refresh, que es red) se resuelve en IO:
    // pedirlo en el hilo principal daría NetworkOnMainThreadException.
    fun runApi(block: suspend (String) -> Unit) {
        busy = true
        error = null
        scope.launch(Dispatchers.IO) {
            val token = runCatching { tidal.auth.getValidAccessToken() }.getOrNull()
            if (token == null) {
                withContext(Dispatchers.Main) {
                    error = "Sesión de Tidal vencida: vuelve a entrar"
                    busy = false
                }
                return@launch
            }
            runCatching { block(token) }
                .onFailure { e -> withContext(Dispatchers.Main) { error = e.message } }
            withContext(Dispatchers.Main) { busy = false }
        }
    }

    fun doSearch() {
        val q = query.trim()
        if (q.isEmpty()) return
        listTracks = null
        listAlbums = null
        myPlaylists = null
        runApi { token ->
            val res = tidal.api.search(token, q)
            withContext(Dispatchers.Main) {
                results = res
                listTitle = "Resultados: $q"
            }
        }
    }

    fun openAlbum(album: TidalAlbum) {
        listAlbums = null
        myPlaylists = null
        results = null
        runApi { token ->
            val tracks = tidal.api.albumTracks(token, album.id)
            withContext(Dispatchers.Main) {
                listTracks = tracks
                listTitle = album.title
            }
        }
    }

    fun openArtist(artist: TidalArtist) {
        listTracks = null
        myPlaylists = null
        results = null
        runApi { token ->
            val albums = tidal.api.artistAlbums(token, artist.id)
            withContext(Dispatchers.Main) {
                listAlbums = albums
                listTitle = artist.name
            }
        }
    }

    fun openPlaylist(playlist: TidalPlaylist) {
        listAlbums = null
        myPlaylists = null
        results = null
        runApi { token ->
            val tracks = tidal.api.playlistTracks(token, playlist.uuid)
            withContext(Dispatchers.Main) {
                listTracks = tracks
                listTitle = playlist.title
            }
        }
    }

    fun loadMyPlaylists() {
        val userId = (authState as? TidalAuth.AuthState.SignedIn)?.userId ?: return
        listTracks = null
        listAlbums = null
        results = null
        runApi { token ->
            val lists = tidal.api.userPlaylists(token, userId)
            withContext(Dispatchers.Main) {
                myPlaylists = lists
                listTitle = "Mis listas"
            }
        }
    }

    fun completeLoginWithCode(code: String) {
        val session = loginSession ?: return
        loginBusy = true
        loginError = null
        scope.launch(Dispatchers.IO) {
            runCatching { tidal.auth.completeLogin(code, session.verifier, session.uniqueKey) }
                .onSuccess {
                    withContext(Dispatchers.Main) {
                        loginSession = null
                        loginBusy = false
                    }
                }
                .onFailure { e ->
                    withContext(Dispatchers.Main) {
                        loginError = e.message ?: "El login falló"
                        loginBusy = false
                    }
                }
        }
    }

    Column(
        Modifier.fillMaxSize().background(MagistralVoid).padding(12.dp)
    ) {
        // ── Barra superior ──────────────────────────────────────────────
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            TextButton(onClick = onBack) { Text("‹ Volver", color = AuroraCyan, fontSize = 15.sp) }
            Text(
                "TIDAL",
                color = MagistralGoldHi,
                fontFamily = MagistralSerif,
                fontSize = 20.sp, fontWeight = FontWeight.ExtraBold
            )
            Spacer(Modifier.weight(1f))
            if (authState is TidalAuth.AuthState.SignedIn) {
                // R · Calidad de streaming: el selector es este ciclista
                // AUTO → MAX HiRes → LOSSLESS → Auto (v2.5.421).
                SpecInfoButton(tidalQualitySpec)
                TextButton(onClick = {
                    // v2.5.421: Auto → MAX HiRes → LOSSLESS → Auto. En Auto
                    // la red decide (WiFi → HiRes, móvil → Lossless).
                    val next = when (quality) {
                        TidalClient.QUALITY_AUTO -> TidalApiClient.QUALITY_HI_RES
                        TidalApiClient.QUALITY_HI_RES -> TidalApiClient.QUALITY_LOSSLESS
                        else -> TidalClient.QUALITY_AUTO
                    }
                    quality = next
                    tidal.qualityPref = next
                }) {
                    Text(
                        when (quality) {
                            TidalClient.QUALITY_AUTO ->
                                if (tidal.quality == TidalApiClient.QUALITY_HI_RES) "Auto · HiRes" else "Auto · Lossless"
                            TidalApiClient.QUALITY_HI_RES -> "MAX HiRes"
                            else -> "LOSSLESS"
                        },
                        color = AuroraCyan, fontSize = 13.sp, fontWeight = FontWeight.Bold
                    )
                }
                TextButton(onClick = { tidal.auth.signOut() }) {
                    Text("Salir", color = TextMuted, fontSize = 13.sp)
                }
            }
        }

        // ── Ficha del entorno · Tidal ───────────────────────────────────
        Spacer(Modifier.height(12.dp))
        ScreenSpecCard(tidalScreenSpec)
        Spacer(Modifier.height(12.dp))

        // El WebView de login se gobierna por loginSession (no por el estado
        // de auth): beginLogin() pone el estado en Authorizing, y si el
        // WebView dependiera del estado, desaparecería al empezar el login.
        if (loginSession != null) {
            LoginWebView(
                authorizeUrl = loginSession!!.authorizeUrl,
                onCode = { code -> completeLoginWithCode(code) }
            )
            if (loginBusy) {
                LinearProgressIndicator(
                    Modifier.fillMaxWidth().padding(top = 8.dp), color = AuroraCyan
                )
            }
            loginError?.let {
                Text(it, color = Color(0xFFFF6B6B), fontSize = 13.sp,
                    modifier = Modifier.padding(top = 8.dp))
            }
            Spacer(Modifier.height(12.dp))
            Text(
                "Si la página no vuelve sola, pega aquí la URL a la que te llevó Tidal:",
                color = TextMuted, fontSize = 12.sp
            )
            OutlinedTextField(
                value = manualUrl,
                onValueChange = { manualUrl = it },
                label = { Text("URL de redirect de Tidal") },
                modifier = Modifier.fillMaxWidth().padding(top = 4.dp),
                singleLine = true
            )
            Button(
                onClick = {
                    val code = TidalAuth.Pkce.extractCodeFromRedirect(manualUrl.trim())
                    if (code != null) completeLoginWithCode(code)
                    else loginError = "Esa URL no trae el código de Tidal"
                },
                modifier = Modifier.padding(top = 8.dp),
                colors = ButtonDefaults.buttonColors(containerColor = AuroraCyan)
            ) {
                Text("Usar esta URL", color = Color.Black)
            }
            TextButton(onClick = { loginSession = null }) {
                Text("Cancelar", color = TextMuted)
            }
        } else when (val auth = authState) {
            is TidalAuth.AuthState.SignedOut, is TidalAuth.AuthState.NeedsRelogin -> {
                if (auth is TidalAuth.AuthState.NeedsRelogin) {
                    Text(
                        "Tu sesión de Tidal venció. Vuelve a entrar.",
                        color = Color(0xFFFFB020), fontSize = 14.sp
                    )
                    Spacer(Modifier.height(8.dp))
                }
                Text(
                    "Tu Tidal dentro de IVANNA. El audio pasa por el motor " +
                        "completo al 100%: sin captura, sin reproductor externo.",
                    color = TextMuted, fontSize = 14.sp
                )
                Spacer(Modifier.height(16.dp))
                Button(
                    onClick = { loginSession = tidal.auth.beginLogin() },
                    colors = ButtonDefaults.buttonColors(containerColor = AuroraCyan)
                ) {
                    Text("Conectar con Tidal", color = Color.Black, fontWeight = FontWeight.Bold)
                }
                Text(
                    "Se abre la página de Tidal: escribe TU usuario y contraseña ahí. " +
                        "IVANNA no ve ni guarda tu contraseña, solo el token de sesión.",
                    color = TextMuted, fontSize = 12.sp, modifier = Modifier.padding(top = 12.dp)
                )
                if (!tidal.store.encryptionAvailable) {
                    Text(
                        "Aviso: el cifrado del teléfono no está disponible; la sesión " +
                            "solo vivirá en memoria y habrá que volver a entrar al cerrar la app.",
                        color = Color(0xFFFFB020), fontSize = 12.sp,
                        modifier = Modifier.padding(top = 8.dp)
                    )
                }
            }

            is TidalAuth.AuthState.Authorizing -> {
                Text("Conectando con Tidal…", color = TextMuted, fontSize = 14.sp)
            }

            is TidalAuth.AuthState.SignedIn -> {
                // R · Calidad de streaming: la regla de AUTO, en claro
                // (el selector es el ciclista de la barra superior).
                Text(
                    "WiFi → HI_RES (FLAC hasta 192 kHz) · Datos móviles → LOSSLESS " +
                        "(FLAC 44.1/16). La red manda.",
                    color = MagistralMuted, fontSize = 12.sp
                )
                Spacer(Modifier.height(8.dp))
                // ── Búsqueda ────────────────────────────────────────────
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    OutlinedTextField(
                        value = query,
                        onValueChange = { query = it },
                        label = { Text("Buscar en Tidal") },
                        modifier = Modifier.weight(1f),
                        singleLine = true,
                        keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
                        keyboardActions = KeyboardActions(onSearch = { doSearch() })
                    )
                    Spacer(Modifier.width(8.dp))
                    Button(
                        onClick = { doSearch() },
                        colors = ButtonDefaults.buttonColors(containerColor = AuroraCyan)
                    ) { Text("Buscar", color = Color.Black) }
                }
                Row(Modifier.fillMaxWidth().padding(top = 8.dp)) {
                    TextButton(onClick = { loadMyPlaylists() }) {
                        Text("Mis listas", color = AuroraCyan, fontSize = 13.sp)
                    }
                }
                if (busy) {
                    LinearProgressIndicator(
                        Modifier.fillMaxWidth().padding(top = 4.dp), color = AuroraCyan
                    )
                }
                error?.let {
                    Text(it, color = Color(0xFFFF6B6B), fontSize = 13.sp,
                        modifier = Modifier.padding(top = 4.dp))
                }
                statusMessage?.let {
                    Text(it, color = Color(0xFFFFB020), fontSize = 13.sp,
                        modifier = Modifier.padding(top = 4.dp))
                }

                // ── S · Prefetch y caché ────────────────────────────────
                // Sin control manual: prefetch y caché trabajan solos en el
                // controlador (v2.5.421). La tarjeta muestra valores reales:
                // uso vivo de tidal.streamCache.sizeBytes() y los topes
                // reales de TidalStreamCache (500 MB / 12 archivos, LRU).
                // El número de archivos en vivo no lo expone ninguna API de
                // esta pantalla: solo se muestra el tope (12). El "312 MB /
                // 8 archivos" del prototipo es un ejemplo de estado, no un
                // valor fijo, y no se reproduce aquí.
                val cacheUsedMb = remember(currentTrack, isLoading) {
                    tidal.streamCache.sizeBytes() / (1024f * 1024f)
                }
                Box(
                    Modifier.fillMaxWidth()
                        .border(1.dp, MagistralLineStrong, RoundedCornerShape(12.dp))
                        .background(MagistralPanel, RoundedCornerShape(12.dp))
                        .padding(12.dp)
                ) {
                    Column {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Text(
                                "Prefetch y caché",
                                color = MagistralGoldHi,
                                fontFamily = MagistralSerif,
                                fontSize = 16.sp, fontWeight = FontWeight.Bold,
                                modifier = Modifier.weight(1f)
                            )
                            SpecInfoButton(tidalPrefetchSpec)
                        }
                        Text(
                            "%.0f MB de 500 MB · hasta 12 archivos · expulsión LRU"
                                .format(cacheUsedMb),
                            color = MagistralText, fontSize = 13.sp,
                            modifier = Modifier.padding(top = 4.dp)
                        )
                        Text(
                            "Las siguientes pistas se descargan mientras suena la actual.",
                            color = MagistralMuted, fontSize = 12.sp
                        )
                    }
                }
                Spacer(Modifier.height(8.dp))

                // ── Resultados / listas ─────────────────────────────────
                LazyColumn(
                    Modifier.weight(1f).fillMaxWidth().padding(top = 8.dp),
                    verticalArrangement = Arrangement.spacedBy(4.dp)
                ) {
                    if (listTitle.isNotEmpty()) {
                        item {
                            Text(
                                listTitle,
                                color = MagistralGoldHi,
                                fontFamily = MagistralSerif,
                                fontSize = 16.sp, fontWeight = FontWeight.Bold
                            )
                        }
                    }
                    listTracks?.let { tracks ->
                        itemsIndexed(tracks) { index, track ->
                            TrackRow(
                                track = track,
                                isCurrent = queueIndex == index && currentTrack?.id == track.id,
                                onClick = { controller.playQueue(tracks, index) }
                            )
                        }
                    }
                    listAlbums?.let { albums ->
                        itemsIndexed(albums) { _, album ->
                            Row(
                                Modifier.fillMaxWidth().clickable { openAlbum(album) }
                                    .padding(vertical = 8.dp, horizontal = 4.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Column(Modifier.weight(1f)) {
                                    Text(album.title, color = TextPrimary, fontSize = 15.sp)
                                    Text(album.artist, color = TextMuted, fontSize = 13.sp)
                                }
                                if (album.trackCount > 0) {
                                    Text("${album.trackCount} temas", color = TextMuted, fontSize = 12.sp)
                                }
                            }
                        }
                    }
                    myPlaylists?.let { lists ->
                        itemsIndexed(lists) { _, playlist ->
                            Row(
                                Modifier.fillMaxWidth().clickable { openPlaylist(playlist) }
                                    .padding(vertical = 8.dp, horizontal = 4.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Column(Modifier.weight(1f)) {
                                    Text(playlist.title, color = TextPrimary, fontSize = 15.sp)
                                    if (playlist.trackCount > 0) {
                                        Text("${playlist.trackCount} temas", color = TextMuted, fontSize = 12.sp)
                                    }
                                }
                            }
                        }
                    }
                    results?.let { res ->
                        if (res.tracks.isNotEmpty()) {
                            item { Text("Canciones", color = MagistralGold, fontFamily = MagistralSerif, fontSize = 14.sp, fontWeight = FontWeight.Bold) }
                            itemsIndexed(res.tracks) { index, track ->
                                TrackRow(
                                    track = track,
                                    isCurrent = queueIndex == index && currentTrack?.id == track.id,
                                    onClick = { controller.playQueue(res.tracks, index) }
                                )
                            }
                        }
                        if (res.albums.isNotEmpty()) {
                            item { Text("Álbumes", color = MagistralGold, fontFamily = MagistralSerif, fontSize = 14.sp, fontWeight = FontWeight.Bold) }
                            itemsIndexed(res.albums) { _, album ->
                                Row(
                                    Modifier.fillMaxWidth().clickable { openAlbum(album) }
                                        .padding(vertical = 8.dp, horizontal = 4.dp)
                                ) {
                                    Column {
                                        Text(album.title, color = TextPrimary, fontSize = 15.sp)
                                        Text(album.artist, color = TextMuted, fontSize = 13.sp)
                                    }
                                }
                            }
                        }
                        if (res.artists.isNotEmpty()) {
                            item { Text("Artistas", color = MagistralGold, fontFamily = MagistralSerif, fontSize = 14.sp, fontWeight = FontWeight.Bold) }
                            itemsIndexed(res.artists) { _, artist ->
                                Row(
                                    Modifier.fillMaxWidth().clickable { openArtist(artist) }
                                        .padding(vertical = 8.dp, horizontal = 4.dp)
                                ) {
                                    Text(artist.name, color = TextPrimary, fontSize = 15.sp)
                                }
                            }
                        }
                        if (res.playlists.isNotEmpty()) {
                            item { Text("Listas", color = MagistralGold, fontFamily = MagistralSerif, fontSize = 14.sp, fontWeight = FontWeight.Bold) }
                            itemsIndexed(res.playlists) { _, playlist ->
                                Row(
                                    Modifier.fillMaxWidth().clickable { openPlaylist(playlist) }
                                        .padding(vertical = 8.dp, horizontal = 4.dp)
                                ) {
                                    Text(playlist.title, color = TextPrimary, fontSize = 15.sp)
                                }
                            }
                        }
                    }
                    if (listTitle.isEmpty() && listTracks == null && listAlbums == null &&
                        myPlaylists == null && results == null
                    ) {
                        item {
                            Text(
                                "Busca una canción, un álbum o un artista. " +
                                    "Al reproducir, el audio pasa por el motor IVANNA completo.",
                                color = TextMuted, fontSize = 14.sp,
                                modifier = Modifier.padding(top = 16.dp)
                            )
                        }
                    }
                }

                // ── Reproduciendo ───────────────────────────────────────
                // T · BridgePlayer: la reproducción de Tidal va por el
                // BridgePlayer (BridgePlayerSink → IvannaBridgePlayer).
                if (currentTrack != null || isLoading) {
                    Box(
                        Modifier.fillMaxWidth()
                            .border(1.dp, MagistralLineStrong, RoundedCornerShape(12.dp))
                            .background(MagistralPanel, RoundedCornerShape(12.dp))
                            .padding(12.dp)
                    ) {
                        Column {
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Text(
                                    "BridgePlayer · Reproductor propio",
                                    color = MagistralGoldHi,
                                    fontFamily = MagistralSerif,
                                    fontSize = 16.sp, fontWeight = FontWeight.Bold,
                                    modifier = Modifier.weight(1f)
                                )
                                SpecInfoButton(tidalBridgePlayerSpec)
                            }
                            Text(
                                "Sesión actual: Tidal → BridgePlayer → cadena magistral al 100 %",
                                color = MagistralMuted, fontSize = 12.sp,
                                modifier = Modifier.padding(top = 2.dp, bottom = 8.dp)
                            )
                            Text(
                                currentTrack?.title ?: "Cargando…",
                                color = MagistralText, fontSize = 16.sp, fontWeight = FontWeight.Bold
                            )
                            Text(
                                listOfNotNull(
                                    currentTrack?.artist,
                                    currentTrack?.album,
                                    qualityLabel.takeIf { it.isNotEmpty() }
                                ).joinToString(" · "),
                                color = TextMuted, fontSize = 13.sp
                            )
                            if (isLoading) {
                                LinearProgressIndicator(
                                    Modifier.fillMaxWidth().padding(top = 8.dp), color = AuroraCyan
                                )
                                Text("Cargando audio de Tidal…", color = TextMuted, fontSize = 12.sp)
                            } else if (durationMs > 0) {
                                Slider(
                                    value = (dragMs ?: positionMs).toFloat(),
                                    onValueChange = { dragMs = it.toLong() },
                                    valueRange = 0f..durationMs.toFloat(),
                                    modifier = Modifier.fillMaxWidth(),
                                    onValueChangeFinished = {
                                        dragMs?.let { player.seekTo(it) }
                                        dragMs = null
                                    }
                                )
                                Text(
                                    "${positionMs / 1000}s / ${durationMs / 1000}s",
                                    color = TextMuted, fontSize = 12.sp
                                )
                            }
                            Row(
                                Modifier.fillMaxWidth().padding(top = 4.dp),
                                horizontalArrangement = Arrangement.SpaceEvenly,
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                TextButton(onClick = { controller.previous() }) {
                                    Text("⏮ Anterior", color = AuroraCyan)
                                }
                                Button(
                                    onClick = { if (isPlaying) controller.pause() else controller.resume() },
                                    colors = ButtonDefaults.buttonColors(containerColor = AuroraCyan)
                                ) {
                                    Text(if (isPlaying) "⏸ Pausa" else "▶ Sonar", color = Color.Black)
                                }
                                TextButton(onClick = { controller.next() }) {
                                    Text("Siguiente ⏭", color = AuroraCyan)
                                }
                                TextButton(onClick = { controller.stop() }) {
                                    Text("■ Parar", color = TextMuted)
                                }
                            }
                            if (queue.isNotEmpty()) {
                                Text(
                                    "Cola: ${queue.size} temas" +
                                        if (queueIndex >= 0) " · sonando ${queueIndex + 1}" else "",
                                    color = TextMuted, fontSize = 12.sp
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun TrackRow(track: TidalTrack, isCurrent: Boolean, onClick: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().clickable { onClick() }
            .padding(vertical = 8.dp, horizontal = 4.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(Modifier.weight(1f)) {
            Text(
                track.title,
                color = if (isCurrent) AuroraCyan else TextPrimary,
                fontSize = 15.sp, fontWeight = if (isCurrent) FontWeight.Bold else FontWeight.Normal
            )
            Text(
                listOf(track.artist, track.album).filter { it.isNotEmpty() }.joinToString(" · "),
                color = TextMuted, fontSize = 13.sp
            )
        }
        if (track.durationSec > 0) {
            Text(
                "${track.durationSec / 60}:${"%02d".format(track.durationSec % 60)}",
                color = TextMuted, fontSize = 12.sp
            )
        }
    }
}

/**
 * WebView SOLO para el login de Tidal. El audio nunca pasa por aquí.
 * Intercepta el redirect a tidal.com/android/login/auth y extrae el `code`
 * (también en onPageFinished: algunos redirects no disparan
 * shouldOverrideUrlLoading).
 */
@SuppressLint("SetJavaScriptEnabled")
@Composable
private fun LoginWebView(authorizeUrl: String, onCode: (String) -> Unit) {
    val context = LocalContext.current
    AndroidView(
        factory = {
            WebView(context).apply {
                settings.javaScriptEnabled = true
                settings.domStorageEnabled = true
                webViewClient = object : WebViewClient() {
                    private fun handleUrl(url: String): Boolean {
                        val code = TidalAuth.Pkce.extractCodeFromRedirect(url)
                        if (code != null) {
                            onCode(code)
                            return true
                        }
                        return false
                    }

                    override fun shouldOverrideUrlLoading(
                        view: WebView, request: WebResourceRequest
                    ): Boolean = handleUrl(request.url.toString())

                    override fun onPageFinished(view: WebView, url: String) {
                        handleUrl(url)
                    }
                }
                loadUrl(authorizeUrl)
            }
        },
        modifier = Modifier.fillMaxWidth().height(480.dp),
        onRelease = { it.destroy() }
    )
}
