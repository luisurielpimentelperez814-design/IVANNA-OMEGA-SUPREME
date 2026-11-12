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
import com.ivanna.omega.ui.theme.ObsidianVoid
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
 */
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
    var quality by remember { mutableStateOf(tidal.quality) }
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
        Modifier.fillMaxSize().background(ObsidianVoid).padding(12.dp)
    ) {
        // ── Barra superior ──────────────────────────────────────────────
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            TextButton(onClick = onBack) { Text("‹ Volver", color = AuroraCyan, fontSize = 15.sp) }
            Text("TIDAL", color = TextPrimary, fontSize = 20.sp, fontWeight = FontWeight.ExtraBold)
            Spacer(Modifier.weight(1f))
            if (authState is TidalAuth.AuthState.SignedIn) {
                TextButton(onClick = {
                    val next = if (quality == TidalApiClient.QUALITY_HI_RES)
                        TidalApiClient.QUALITY_LOSSLESS else TidalApiClient.QUALITY_HI_RES
                    quality = next
                    tidal.quality = next
                }) {
                    Text(
                        if (quality == TidalApiClient.QUALITY_HI_RES) "MAX HiRes" else "LOSSLESS",
                        color = AuroraCyan, fontSize = 13.sp, fontWeight = FontWeight.Bold
                    )
                }
                TextButton(onClick = { tidal.auth.signOut() }) {
                    Text("Salir", color = TextMuted, fontSize = 13.sp)
                }
            }
        }

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

                // ── Resultados / listas ─────────────────────────────────
                LazyColumn(
                    Modifier.weight(1f).fillMaxWidth().padding(top = 8.dp),
                    verticalArrangement = Arrangement.spacedBy(4.dp)
                ) {
                    if (listTitle.isNotEmpty()) {
                        item { Text(listTitle, color = TextPrimary, fontSize = 16.sp, fontWeight = FontWeight.Bold) }
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
                            item { Text("Canciones", color = AuroraCyan, fontSize = 14.sp, fontWeight = FontWeight.Bold) }
                            itemsIndexed(res.tracks) { index, track ->
                                TrackRow(
                                    track = track,
                                    isCurrent = queueIndex == index && currentTrack?.id == track.id,
                                    onClick = { controller.playQueue(res.tracks, index) }
                                )
                            }
                        }
                        if (res.albums.isNotEmpty()) {
                            item { Text("Álbumes", color = AuroraCyan, fontSize = 14.sp, fontWeight = FontWeight.Bold) }
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
                            item { Text("Artistas", color = AuroraCyan, fontSize = 14.sp, fontWeight = FontWeight.Bold) }
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
                            item { Text("Listas", color = AuroraCyan, fontSize = 14.sp, fontWeight = FontWeight.Bold) }
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
                if (currentTrack != null || isLoading) {
                    Box(
                        Modifier.fillMaxWidth()
                            .border(1.dp, AuroraCyan.copy(alpha = 0.4f), RoundedCornerShape(12.dp))
                            .background(Color(0xFF0A1420), RoundedCornerShape(12.dp))
                            .padding(12.dp)
                    ) {
                        Column {
                            Text(
                                currentTrack?.title ?: "Cargando…",
                                color = TextPrimary, fontSize = 16.sp, fontWeight = FontWeight.Bold
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
