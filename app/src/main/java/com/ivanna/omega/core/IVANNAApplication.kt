package com.ivanna.omega.core
import com.ivanna.omega.audio.AudioStateManager

import android.app.Application
import android.util.Log
import com.ivanna.omega.audio.IvannaGlobalEffectManager
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.magisk.OmegaDaemon
import com.ivanna.omega.magisk.OmegaEngineBridge
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.channels.Channel
import android.content.IntentFilter
import android.media.audiofx.AudioEffect
import com.ivanna.omega.audio.AudioSessionReceiver
import com.ivanna.omega.audio.IvannaControlLoop
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/**
 * IVANNAApplication — Punto de entrada de la aplicación.
 *
 * FIXES DE CONECTIVIDAD:
 *   1. Expone globalEffectManager como propiedad pública para que
 *      AudioSessionReceiver pueda acceder a él via applicationContext.
 *   2. Inicializa globalEffectManager ANTES del OmegaDaemon para que
 *      las primeras sesiones de audio ya tengan efectos disponibles.
 *   3. isInitialized es Thread-safe (@Volatile).
 *   4. onTerminate() libera globalEffectManager correctamente.
 */
class IVANNAApplication : Application() {

    companion object {
        private const val TAG = "IVANNAApplication"
        val appScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

        /**
         * Canal CONFLATED: sustituye appScope.launch x N en pushToNative().
         * Con CONFLATED solo el ultimo valor de parametros DSP llega al socket;
         * los intermedios se descartan. Elimina el crash por OOM (coroutines
         * bloqueadas en connect() CONNECT_TIMEOUT_MS=2000ms a 60fps = miles de
         * threads IO bloqueados despues de minutos de uso).
         */
        val pfParamChannel = Channel<FloatArray>(Channel.CONFLATED)
        val omegaBridge = OmegaEngineBridge

        @Volatile
        var isInitialized = false
            private set
    }

    // FIX: expuesto como propiedad de instancia (no companion) para que
    // AudioSessionReceiver lo acceda via (context.applicationContext as IVANNAApplication)
    val globalEffectManager = IvannaGlobalEffectManager()

    // Expuesto para que la UI o un futuro entry-point de STT puedan invocar
    // routing por comando/clasificación sin recrear el clasificador YAMNet.
    // Se inicializa perezosamente porque VoiceController carga YAMNet en
    // construcción y no queremos pagar ese coste si nadie lo usa.
    val voiceController: com.ivanna.omega.VoiceController by lazy {
        com.ivanna.omega.VoiceController(this)
    }

    override fun onCreate() {
        super.onCreate()
        AudioStateManager.attachPersistence(this)
        Log.d(TAG, "=== IVANNA DSP Application iniciada ===")

        // FIX (crash): consumer unico del canal DSP.
        // Un solo socket write activo a la vez; CONFLATED descarta los intermedios.
        appScope.launch {
            for (params in pfParamChannel) {
                runCatching {
                    omegaBridge.setPFParams(
                        params[0], params[1], params[2],
                        params[3], params[4], params[5],
                        params[6], params[7],
                        params[8], params[9], params[10], params[11], params[12]
                    )
                }
            }
        }

        // Conectar DSPState con GlobalEffectManager ANTES de que la UI cargue.
        // Sin esto, pushToNative() nunca llama adjustLiveParams() y los sliders
        // de EQ/Width/Exciter/Comp no afectan Spotify/YouTube/ninguna app externa.
        com.ivanna.omega.dsp.DSPState.globalEffectManager = globalEffectManager

        // Inicializar el optimizador Riemanniano SAF y el gestor de HRTF en background
        com.ivanna.omega.spatial.SaFOptimizer.init(this)
        com.ivanna.omega.spatial.IvannaSpatialManager.init(this)

        // FIX (controles Android 13+): registrar AudioSessionReceiver dinámicamente
        // con RECEIVER_NOT_EXPORTED ademas del Manifest, ya que en API33+
        // el sistema puede no enviar broadcasts implícitos a receivers solo de Manifest.
        runCatching {
            val filter = IntentFilter().apply {
                addAction(AudioEffect.ACTION_OPEN_AUDIO_EFFECT_CONTROL_SESSION)
                addAction(AudioEffect.ACTION_CLOSE_AUDIO_EFFECT_CONTROL_SESSION)
            }
            val flags = if (android.os.Build.VERSION.SDK_INT >= 33)
                android.content.Context.RECEIVER_NOT_EXPORTED else 0
            registerReceiver(AudioSessionReceiver(), filter, flags)
        }.onFailure { Log.w("IVANNAApp", "AudioSessionReceiver dyn reg failed: ${it.message}") }

        // FIX: OmegaEngine se inicializa con el Context ANTES del scope IO
        OmegaEngine.init(this)

        // FIX (audit): SR real del hardware + inicialización del motor NPE.
        // Sin esto:
        //   - OmegaMetrics.sampleRate quedaba en el default (48 kHz) aunque
        //     el hardware reporte 96 kHz o distinto.
        //   - IvannaNpeEngine.handle == 0L → getMetrics() devolvía FloatArray(8)
        //     de ceros → RMS=-60 dB / AGC=0 dB / clasificación=— 0%.
        //   Ni IvannaBridgePlayer ni PlaybackCaptureService lo inicializan
        //   — sólo lo consumen. El único sitio garantizado a correr una vez
        //   antes que la UI es Application.onCreate.
        //
        // maxBlockFrames = 2048: mismo tope usado por PlaybackCaptureService y
        // BridgePlayer (MAX_CHUNK_FRAMES). Mayor de lo estrictamente necesario
        // no cuesta memoria significativa y evita re-init si el bloque crece.
        val realSampleRate = try {
            val am = getSystemService(AUDIO_SERVICE) as android.media.AudioManager
            am.getProperty(android.media.AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE)
                ?.toIntOrNull() ?: 48000
        } catch (_: Throwable) { 48000 }
        com.ivanna.omega.audio.OmegaMetrics.updateSampleRate(realSampleRate)
        Log.i(TAG, "HW sample rate detectado = ${realSampleRate}Hz")

        runCatching {
            com.ivanna.omega.neuromorphic.IvannaNpeEngine.init(realSampleRate, 2048)
            if (com.ivanna.omega.neuromorphic.IvannaNpeEngine.isReady)
                Log.i(TAG, "✅ IvannaNpeEngine inicializado — telemetría viva")
            else
                Log.w(TAG, "⚠️ IvannaNpeEngine no listo (libivanna_omega.so ausente)")
        }.onFailure {
            Log.e(TAG, "IvannaNpeEngine.init falló: ${it.message}", it)
        }

        // FIX (rehabilitación — Prioridad 1.5 más alta de
        // IVANNA_ARCHITECTURE_DECISION_REPORT.md): AudioRouteManager nunca
        // se instanciaba en todo el repo pese a que su destino
        // (control_set_route_profile() en audio_control_plane.hpp) ya
        // estaba confirmado vivo y consumido de verdad por
        // control_apply_frame() (route_bass_boost alimenta f.low,
        // route_dialog_boost alimenta el EQ combinado, route_widener_mult
        // el ancho estéreo — verificado línea por línea antes de conectar
        // esto). Sin el manager arrancado, la compensación por ruta de
        // salida (Bluetooth SBC/AAC lossy, rolloff de graves en AUX) nunca
        // se activaba, aunque el motor que la aplica funcionaba bien.
        //
        // DEBE ser síncrono en el hilo principal, NO dentro de
        // appScope.launch (Dispatchers.IO): AudioManager.
        // registerAudioDeviceCallback(callback, null) usa el Looper del
        // hilo que llama para entregar los callbacks — un hilo del pool de
        // IO no tiene Looper preparado, lo que puede fallar en tiempo de
        // ejecución. Application.onCreate() corre garantizado en el hilo
        // principal (con Looper), por eso va aquí y no más abajo.
        com.ivanna.omega.audio.AudioRouteManager.start(this)
        com.ivanna.omega.audio.IvannaUnifiedPipeline.start(this)

        // FIX: IvannaControlLoop.start() nunca se llamaba desde IVANNAApplication.
        // El loop 20Hz (nativeApplyControlFrame + nativeSetLearningContext)
        // existía completo en IvannaControlLoop.kt pero nunca arrancaba:
        // el motor de aprendizaje nunca aplicaba su control frame ni actualizaba
        // el contexto de género — la IA corría en vacío.
        // Se lanza DESPUÉS de IvannaNpeEngine.init() (que provee getDetectedGenre)
        // y ANTES de appScope.launch para que el primer tick ya tenga NPE listo.
        IvannaControlLoop.start()

        // FIX (carrera): esto DEBE ser síncrono, no ir dentro de appScope.launch.
        // MainActivity.onCreate() llama a IvannaNativeLib.nativeStartEvoThread()
        // directamente (si evo_enabled) en el hilo principal, sin esperar a
        // Application.appScope (Dispatchers.IO) — si el path se fijara ahí,
        // llegaría tarde la mayoría de las veces y evo_initialize_population()
        // correría con g_savePath vacío (persistencia deshabilitada de facto).
        if (IvannaNativeLib.isLoaded) {
            IvannaNativeLib.nativeSetEvoSavePath(
                "${filesDir.absolutePath}/evo_population.bin"
            )
        }

        appScope.launch {
            try {
                // 1. DSP nativo
                DSPBridge.init(sampleRate = 96000)
                Log.d(TAG, "✅ DSPBridge listo — 96000 Hz")

                // 2. Daemon Magisk (puede fallar sin root — no es fatal)
                // FIX: runCatching aísla UnsatisfiedLinkError (símbolos JNI del daemon
                // ausentes en la .so) del bloque principal. Sin esto, el outer
                // catch(UnsatisfiedLinkError) abortaría el connect() y el loop
                // de reconexión de 5s — el socket nunca se abriría aunque el
                // daemon system-wide estuviera corriendo en Magisk.
                val daemonOk = runCatching { OmegaDaemon.start() }.getOrElse { false }
                Log.d(TAG, if (daemonOk) "✅ OmegaDaemon iniciado"
                           else          "⚠️ OmegaDaemon no disponible (modo no-root activo)")

                // 3. FIX CRÍTICO: probe real al socket + loop de reconexión.
                // connect() anterior era fake (isConnected=true sin tocar socket).
                // Ahora hace un LocalSocket probe real; si falla en esta primera
                // pasada (daemon Magisk aún arrancando), el loop de 5s reintenta.
                delay(500)
                omegaBridge.connect()
                Log.d(TAG, if (omegaBridge.isConnected)
                    "✅ OmegaEngineBridge conectado al daemon system-wide"
                else
                    "⚪ OmegaEngineBridge: daemon no disponible (modo no-root)"
                )
                // Loop de reconexión: reintenta cada 5s si el daemon sube tarde
                // (e.g. Magisk late_start_service). Solo dispara probes ligeros.
                launch {
                    while (true) {
                        delay(5_000L)
                        if (!omegaBridge.isConnected) {
                            val ok = omegaBridge.connect()
                            if (ok) Log.i(TAG, "✅ OmegaEngineBridge reconectado al daemon")
                        }
                    }
                }

                isInitialized = true
                Log.i(TAG, "✅ IVANNA-OMEGA-SUPREME lista")
            } catch (e: UnsatisfiedLinkError) {
                Log.e(TAG, "❌ Librería nativa no disponible: ${e.message}")
            } catch (e: Exception) {
                Log.e(TAG, "❌ Error de inicialización: ${e.message}")
            }
        }

        // Sync de perfiles en la nube: no-op seguro si CloudSyncManager no
        // está configurado todavía (ver CloudSyncManager.kt). Separado del
        // bloque de arriba a propósito — no debe bloquear ni afectar el
        // arranque del motor DSP si falla o tarda (red lenta, etc.).
        appScope.launch {
            try {
                CloudSyncManager.syncDown(
                      this@IVANNAApplication,
                      UserProfileManager()
                  )
            } catch (e: Exception) {
                Log.w(TAG, "syncDown en arranque falló (no crítico): ${e.message}")
            }
        }

        // FIX (audit): puente YAMNet global → OmegaMetrics.
        // pollOmegaMetrics() sólo corre cuando el BridgePlayer está
        // reproduciendo — en captura del sistema o standby nadie refresca
        // la categoría, así que la UI mostraba "—  0%" para siempre. Este
        // collector reenvía AudioPipeline.sharedYamnetResult (que sí se
        // actualiza en classifyAndRoute()) al StateFlow compartido de
        // OmegaMetrics. Confidence = mejor de los 3 scores; la etiqueta
        // legible viene del NPE si está listo (nativeGetDetectedGenre()),
        // con fallback a la categoría dominante speech/music/bass.
        appScope.launch {
            com.ivanna.omega.audio.AudioPipeline.sharedYamnetResult.collect { r ->
                if (!r.valid) return@collect
                val dominant = when {
                    r.speech >= r.music && r.speech >= r.bass -> "speech"
                    r.music  >= r.bass                        -> "music"
                    else                                       -> "bass"
                }
                val label = try {
                    if (com.ivanna.omega.neuromorphic.IvannaNpeEngine.isReady)
                        com.ivanna.omega.neuromorphic.IvannaNpeEngine.getDetectedGenre()
                            .ifBlank { dominant }
                    else dominant
                } catch (_: Throwable) { dominant }
                val conf = maxOf(r.speech, r.music, r.bass)
                com.ivanna.omega.audio.OmegaMetrics.updateSharedYamnet(label, conf)
            }
        }
    }

    override fun onTerminate() {
        // Best-effort: onTerminate() no está garantizado en dispositivos reales,
        // pero el autosave periódico en evolveGeneration() ya cubre el caso de
        // que el proceso muera sin pasar por aquí.
        if (IvannaNativeLib.isLoaded) {
            try {
                IvannaNativeLib.nativeSaveEvoState()
            } catch (e: Exception) {
                Log.e(TAG, "⚠️ No se pudo guardar evo state en onTerminate: ${e.message}")
            }
        }
        globalEffectManager.releaseAll()
        omegaBridge.disconnect()
        OmegaDaemon.stop()
        IvannaControlLoop.stop()
        com.ivanna.omega.audio.AudioRouteManager.stop()
        runCatching { com.ivanna.omega.neuromorphic.IvannaNpeEngine.release() }
        super.onTerminate()
    }
}
