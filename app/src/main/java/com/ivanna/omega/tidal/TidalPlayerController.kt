package com.ivanna.omega.tidal

import android.net.Uri
import com.ivanna.omega.audio.IvannaBridgePlayer
import kotlinx.coroutines.cancel
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import java.io.File

/**
 * TIDAL — controlador de reproducción (v2.5.416).
 *
 * La cola guarda IDs de Tidal, no URLs: la URL se resuelve FRESCA en el
 * momento de reproducir cada tema (las URLs firmadas caducan; patrón de
 * cliamp). El audio pasa por el BridgePlayer existente (su decoder ya
 * maneja FLAC vía MediaExtractor/MediaCodec) → cadena magistral al 100%.
 *
 * Fin de tema: el BridgePlayer pone state=STOPPED al agotar el extractor
 * (EOS). El controlador distingue ese STOPPED del STOPPED transitorio que
 * produce play() (que llama a stop() internamente) y del stop() del
 * usuario, con el flag `suppressNextStop`: se limpia al observar PLAYING
 * o al observar el STOPPED transitorio; el STOPPED real con wasPlaying
 * avanza a la siguiente pista. Si la cola se acaba, se avisa.
 *
 * El sink es una interfaz para que los tests usen un reproductor falso sin
 * cargar MediaExtractor/MediaCodec. El adaptador real (BridgePlayerSink)
 * traduce IvannaBridgePlayer.State al enum propio del controlador.
 */
enum class TidalSinkState { IDLE, PLAYING, PAUSED, STOPPED, ERROR }

interface TidalPlayerSink {
    val state: TidalSinkState
    fun play(uri: Uri)
    fun pause()
    fun resume()
    fun stop()
}

class BridgePlayerSink(private val player: IvannaBridgePlayer) : TidalPlayerSink {
    override val state: TidalSinkState
        get() = when (player.state) {
            IvannaBridgePlayer.State.PLAYING -> TidalSinkState.PLAYING
            IvannaBridgePlayer.State.PAUSED -> TidalSinkState.PAUSED
            IvannaBridgePlayer.State.STOPPED -> TidalSinkState.STOPPED
            IvannaBridgePlayer.State.ERROR -> TidalSinkState.ERROR
            IvannaBridgePlayer.State.IDLE -> TidalSinkState.IDLE
        }

    override fun play(uri: Uri) = player.play(uri)
    override fun pause() = player.pause()
    override fun resume() = player.resume()
    override fun stop() = player.stop()
}

class TidalPlayerController(
    private val sink: TidalPlayerSink,
    private val api: TidalApiClient,
    private val auth: TidalAuth,
    private val resolver: TidalStreamResolver,
    private val cacheDir: File,
    private val scope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
) {

    private val _currentTrack = MutableStateFlow<TidalTrack?>(null)
    val currentTrack: StateFlow<TidalTrack?> = _currentTrack.asStateFlow()

    private val _queue = MutableStateFlow<List<TidalTrack>>(emptyList())
    val queue: StateFlow<List<TidalTrack>> = _queue.asStateFlow()

    private val _queueIndex = MutableStateFlow(-1)
    val queueIndex: StateFlow<Int> = _queueIndex.asStateFlow()

    private val _qualityLabel = MutableStateFlow("")
    val qualityLabel: StateFlow<String> = _qualityLabel.asStateFlow()

    private val _statusMessage = MutableStateFlow<String?>(null)
    val statusMessage: StateFlow<String?> = _statusMessage.asStateFlow()

    private val _isPlaying = MutableStateFlow(false)
    val isPlaying: StateFlow<Boolean> = _isPlaying.asStateFlow()

    private val _isLoading = MutableStateFlow(false)
    /** true mientras se resuelve el stream (red + descarga DASH). */
    val isLoading: StateFlow<Boolean> = _isLoading.asStateFlow()

    /** Calidad pedida al servidor (LOSSLESS por defecto; MAX = HI_RES_LOSSLESS). */
    var quality: String = TidalApiClient.DEFAULT_QUALITY
        set(value) {
            field = when (value) {
                TidalApiClient.QUALITY_LOW, TidalApiClient.QUALITY_HIGH,
                TidalApiClient.QUALITY_LOSSLESS, TidalApiClient.QUALITY_HI_RES -> value
                else -> TidalApiClient.DEFAULT_QUALITY
            }
        }

    @Volatile
    private var suppressNextStop = false

    @Volatile
    private var wasPlaying = false

    @Volatile
    private var lastObserved: TidalSinkState = TidalSinkState.IDLE

    private var pendingDownload: File? = null
    private var playJob: Job? = null

    // ── Acciones de UI ────────────────────────────────────────────────────

    fun playQueue(tracks: List<TidalTrack>, startIndex: Int = 0) {
        if (tracks.isEmpty()) return
        _queue.value = tracks
        playAt(startIndex.coerceIn(0, tracks.size - 1))
    }

    fun playTrack(track: TidalTrack) {
        _queue.value = listOf(track)
        playAt(0)
    }

    fun next() {
        val next = _queueIndex.value + 1
        if (next < _queue.value.size) playAt(next)
        else _statusMessage.value = "Fin de la cola"
    }

    fun previous() {
        val prev = _queueIndex.value - 1
        if (prev >= 0) playAt(prev) else playAt(_queueIndex.value.coerceAtLeast(0))
    }

    fun pause() {
        runCatching { sink.pause() }
        _isPlaying.value = false
    }

    fun resume() {
        runCatching { sink.resume() }
        _isPlaying.value = true
    }

    fun stop() {
        suppressNextStop = true
        wasPlaying = false
        playJob?.cancel()
        playJob = null
        runCatching { sink.stop() }
        _isPlaying.value = false
        _isLoading.value = false
        _currentTrack.value = null
        _queueIndex.value = -1
    }

    /** Cierra el scope. El temporal DASH pendiente NO se borra aquí: el
     * reproductor puede seguir leyéndolo mientras su job se cancela; los
     * temporales viejos se limpian al crear el próximo TidalClient. */
    fun close() {
        playJob?.cancel()
        scope.cancel()
        pendingDownload = null
    }

    // ── Observación del estado del sink (la UI lo llama cada ~100 ms) ────

    fun onPlayerStateChanged(state: TidalSinkState) {
        lastObserved = state
        when (state) {
            TidalSinkState.PLAYING -> {
                wasPlaying = true
                suppressNextStop = false
                _isPlaying.value = true
                _isLoading.value = false
            }
            TidalSinkState.PAUSED -> {
                _isPlaying.value = false
            }
            TidalSinkState.STOPPED -> {
                _isPlaying.value = false
                if (suppressNextStop) {
                    suppressNextStop = false
                    return
                }
                if (wasPlaying && _currentTrack.value != null) {
                    wasPlaying = false
                    advance()
                }
            }
            TidalSinkState.ERROR -> {
                _isPlaying.value = false
                _isLoading.value = false
                if (_currentTrack.value != null && !suppressNextStop) {
                    _statusMessage.value = "El reproductor falló con esta pista"
                }
            }
            TidalSinkState.IDLE -> Unit
        }
    }

    // ── Internos ──────────────────────────────────────────────────────────

    private fun advance() {
        val next = _queueIndex.value + 1
        if (next < _queue.value.size) {
            playAt(next)
        } else {
            _queueIndex.value = -1
            _currentTrack.value = null
            _statusMessage.value = "Fin de la cola"
        }
    }

    private fun playAt(index: Int) {
        val tracks = _queue.value
        if (index !in tracks.indices) return
        val track = tracks[index]
        // Síncrono, antes de cualquier observación del poll: el STOPPED
        // transitorio del play() interno no debe disparar el avance.
        suppressNextStop = true
        wasPlaying = false
        _queueIndex.value = index
        _currentTrack.value = track
        _statusMessage.value = null
        _isLoading.value = true
        playJob?.cancel()
        playJob = scope.launch {
            try {
                val token = auth.getValidAccessToken()
                if (token == null) {
                    _statusMessage.value = "Sesión de Tidal vencida: vuelve a entrar"
                    _currentTrack.value = null
                    _isLoading.value = false
                    return@launch
                }
                val info = api.playbackInfo(token, track.id, quality)
                val resolved = resolver.resolve(info, cacheDir)
                _qualityLabel.value = resolved.qualityLabel
                val oldFile = pendingDownload
                pendingDownload = resolved.downloadedFile
                sink.play(Uri.parse(resolved.playbackUri))
                oldFile?.let { runCatching { it.delete() } }
            } catch (e: TidalEncryptedException) {
                _statusMessage.value = e.message
                _currentTrack.value = null
                _isLoading.value = false
            } catch (e: TidalApiException) {
                _statusMessage.value = e.message ?: "Tidal no respondió"
                _currentTrack.value = null
                _isLoading.value = false
            } catch (e: Exception) {
                _statusMessage.value = "No se pudo reproducir: ${e.message}"
                _currentTrack.value = null
                _isLoading.value = false
            }
        }
    }
}
