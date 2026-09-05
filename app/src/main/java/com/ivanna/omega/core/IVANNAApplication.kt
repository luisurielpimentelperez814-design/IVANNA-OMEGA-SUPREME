package com.ivanna.omega.core
import com.ivanna.omega.audio.IvannaGlobalEffectManager
import com.ivanna.omega.audio.AudioStateManager

import android.app.Application
import android.util.Log
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.magisk.OmegaDaemon
import com.ivanna.omega.audio.Iso226Calibrator
import com.ivanna.omega.magisk.MagiskBridge
import com.ivanna.omega.magisk.OmegaEngineBridge
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.channels.Channel
import android.content.IntentFilter
import android.media.audiofx.AudioEffect
import com.ivanna.omega.audio.AudioSessionReceiver
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import com.ivanna.omega.core.OEMTelemetry


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
    
    private lateinit var paramStore: ParameterStore
    @Volatile var lastNativeSampleRate: Int = 48000
        private set


    companion object {
        lateinit var instance: IVANNAApplication

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
    val globalEffectManager = IvannaGlobalEffectManager(this)

    // FIX (build, log CI 84498918857): había DOS declaraciones de paramStore
    // en conflicto — la lateinit de la línea 34 (core.ParameterStore, mismo
    // paquete, inicializada en onCreate:77) y esta lazy delegada a
    // audio.ParameterStore. Kotlin no permite dos miembros con el mismo
    // nombre → 'Conflicting declarations'. Los únicos usos reales de
    // paramStore (líneas 235-237: loadIso226Calibrated/ListenPhon/RefPhon)
    // existen SOLO en core.ParameterStore (verificado: audio.ParameterStore
    // solo tiene saveParameters/loadParameters). Se conserva la lateinit
    // de core y se elimina esta duplicada.

    // ── Cerebro perceptual — singleton de aplicación ──────────────────────
    // PerceptualCortex procesa PCM → ISO 226 → Bark → EQ → DSP en tiempo real
    // PerceptualBrainEngine hace polling de telemetría nativa cada 100ms
    val perceptualCortex by lazy { com.ivanna.omega.ai.PerceptualCortex() }
    val perceptualBrainEngine by lazy { com.ivanna.omega.ai.PerceptualBrainEngine() }

    // Expuesto para que la UI o un futuro entry-point de STT puedan invocar
    // routing por comando/clasificación sin recrear el clasificador YAMNet.
    // Se inicializa perezosamente porque VoiceController carga YAMNet en
    // construcción y no queremos pagar ese coste si nadie lo usa.
    val voiceController: com.ivanna.omega.VoiceController by lazy {
        com.ivanna.omega.VoiceController(this)
    }

    override fun onCreate() {
        instance = this

        paramStore = ParameterStore(this)
        // Polish: consumir la SR nativa persistida (9f99d4e6 la guardaba sin
        // consumidor). Application tiene Context — unico punto seguro para
        // leerla antes de cualquier init de motor. La SR guardada sobrevive
        // reinicios; si nunca se guardo, loadNativeSampleRate() = 48000.
        lastNativeSampleRate = paramStore.loadNativeSampleRate()

        // Primer arranque: aplica IVANNA_OMEGA_SIGNATURE antes de cualquier
        // restore — el restoreToNative() posterior lo empuja al DSP nativo.
        // Si el usuario ya tenía configuración, no se toca nada (flag).
        if (paramStore.applySignaturePresetIfFirstRun()) {
            Log.i(TAG, "✨ Preset IVANNA_OMEGA_SIGNATURE aplicado (primer inicio)")
        }

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

        // FIX: restaurar calibración ISO 226 al arrancar.
        // Sin esto la curva de compensación perceptual (equal-loudness)
        // se perdía en cada reinicio — los efectos globales de Spotify/YouTube
        // volvían a flat aunque el usuario ya hubiera calibrado su escucha.
        runCatching {
            val restored = Iso226Calibrator.restoreIfSaved(this, globalEffectManager)
            Log.d(TAG, if (restored) "✅ ISO 226 restaurado" else "⚪ ISO 226 sin calibración previa")
        }.onFailure { Log.w(TAG, "ISO 226 restore falló (no crítico): ${it.message}") }

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
        // TAREA 4: la detección de ruta de AudioRouteManager gobierna ahora el
        // DSP real (HRTF / sala RIR / ancho espacial) vía RouteDspCalibrator.
        // No modifica AudioRouteManager — lee su detectOutputRoute() público.
        com.ivanna.omega.audio.RouteDspCalibrator.start(this)
        com.ivanna.omega.audio.IvannaUnifiedPipeline.start(this)

        // Gobernador térmico OEM: degrada carga DSP (exciter → espacial →
        // compresor) ANTES de que el kernel haga throttling de golpe — mejor
        // bajar calidad que producir un glitch. Inerte en API<29 o si el
        // HAL térmico no responde (sin efecto colateral).
        com.ivanna.omega.audio.ThermalGovernor.start(this)

        // FIX (root vs sin root): hasta ahora la app asumia siempre el camino
        // con root (daemon Magisk por socket). Sin root, MagiskBridge devolvia
        // "queued" y OmegaEngineBridge no conectaba: nada procesaba el audio.
        // AudioBackendSelector hace el probe real de `su` en background y
        // levanta el fallback AudioEffect/DynamicsProcessing cuando toca.
        com.ivanna.omega.audio.AudioBackendSelector.start(this)

        // ── Arrancar cerebro perceptual ───────────────────────────────────
        // Forzar inicialización lazy + arrancar polling de telemetría 10Hz
        appScope.launch(kotlinx.coroutines.Dispatchers.Default) {
            runCatching {
                perceptualBrainEngine.start()
                Log.i(TAG, "✅ PerceptualBrainEngine activo — polling 10Hz")
            }.onFailure { Log.w(TAG, "PerceptualBrainEngine: ${it.message}") }
            runCatching {
                // Forzar init lazy del cortex
                perceptualCortex.toString()
                Log.i(TAG, "✅ PerceptualCortex listo")
            }.onFailure { Log.w(TAG, "PerceptualCortex: ${it.message}") }
        }

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
                // 1. DSP nativo — SR REAL del hardware, no hardcodeado.
                // FIX (hi-res 48/96/192/384k): antes estaba fijo a 96000 Hz.
                // Si el dispositivo corre a 48 kHz, TODOS los filtros del DSP
                // (EQ biquad, compresor, excitador, widener) se calculaban con
                // la tasa equivocada → bandas EQ desplazadas ×2 en frecuencia,
                // envelopes del compresor 2× más rápidos, y cualquier módulo
                // con fase acumulada (NHO/PhaseOracle) derivando el tono.
                // Esa es una fuente directa de distorsión de tono/reportada.
                // PROPERTY_OUTPUT_SAMPLE_RATE reporta la tasa REAL del mix
                // del HAL (48/96/192k según el dispositivo y la ruta activa);
                // 48000 solo como último fallback defensivo.
                // FIX (SAF/SOFA/HRTF/RIR nunca se usaban sin root, 2026-09-04):
                // los 371 MB de datasets ya iban empaquetados en assets/ pero
                // IvannaAssetProvider —la clase que los extrae a filesDir para
                // acceso sin root— no tenía ningún caller en todo el proyecto
                // (verificado con grep). Y aunque se llamara, TODO loader nativo
                // (RirDataset, HrtfManager, SaFOptimizer, ivanna_object_renderer,
                // omega_effect) solo conocía rutas root: /data/adb/ivanna_omega
                // y /system/etc/ivanna_omega. RirDataset::load() ya prueba ahora
                // /data/data/com.ivanna.omega/files/ivanna_omega/rir como
                // segundo candidato — esta llamada es la que efectivamente pone
                // los ficheros ahí ANTES de que DSPBridge.init() intente leerlos.
                // Debe ir antes de DSPBridge.init(): correr en Dispatchers.IO
                // porque copia ficheros (potencialmente grandes) la primera vez.
                runCatching {
                    kotlinx.coroutines.runBlocking(kotlinx.coroutines.Dispatchers.IO) {
                        com.ivanna.omega.core.IvannaAssetProvider.modelDir(this@IVANNAApplication)
                    }
                }.onFailure { Log.w(TAG, "Extracción de assets SAF/SOFA/HRTF/RIR falló (no fatal): ${it.message}") }

                val hwSr = (getSystemService(AUDIO_SERVICE) as android.media.AudioManager)
                    .getProperty(android.media.AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE)
                    ?.toIntOrNull() ?: 48000
                DSPBridge.init(sampleRate = hwSr)
                Log.d(TAG, "✅ DSPBridge listo — $hwSr Hz (hardware real)")

                // Capa agéntica (2026-08-31): arranca los 5 agentes sobre los
                // canales ya inicializados (DSPBridge in-process + puente al
                // daemon). Opera a cadencia lenta (1 s) en Dispatchers.Default,
                // nunca en el hilo de audio; si la librería nativa aún no está
                // lista, AcousticPerceptionAgent devuelve snapshot vacío y el
                // ciclo no interviene (política neutral) hasta que lo esté.
                // attachContext ANTES de start(): sin él el agente corría sin
                // SharedPreferences (prefs == null) y la persistencia de
                // decisiones/política quedaba inerte pese a estar implementada.
                runCatching {
                    com.ivanna.omega.agent.IvannaAgentCore.attachContext(this@IVANNAApplication)
                    com.ivanna.omega.agent.IvannaAgentCore.start()
                }.onFailure { Log.w(TAG, "AgentCore no arrancó (no fatal): ${it.message}") }

                // Auto-reparación (2026-09-01): el SelfHealingAgent vigila el
                // daemon, el motor adaptativo y el clipping sostenido, y actúa
                // sin intervención humana (cooldowns anti-bucle). Con gracia
                // inicial de 8 s interna para no diagnosticar el arranque en
                // frío como fallo.
                runCatching { com.ivanna.omega.agent.SelfHealingAgent.start() }
                    .onFailure { Log.w(TAG, "SelfHealingAgent no arrancó (no fatal): ${it.message}") }

                // 2. Daemon Magisk (puede fallar sin root — no es fatal).
                // FIX (re-aplicado): OmegaDaemon.kt declara 18 external fun sin
                // NINGUNA implementación JNI en C++ (verificado con grep: 0
                // símbolos magisk_OmegaDaemon_* en app/src/main/cpp/). Por tanto
                // nativeStart() lanza UnsatisfiedLinkError en CADA arranque, y
                // al estar dentro del try principal la excepción saltaba por
                // encima de omegaBridge.connect() y del loop de reconexión — el
                // puente por socket al daemon system-wide nunca se conectaba
                // desde el arranque. La vía viva es OmegaEngineBridge por
                // socket (sendPerceptualState / setPFParams / pushSAFState).
                // Se aísla en runCatching para que el arranque continúe.
                // (Fue perdido tras rebase en commits posteriores a 602af12.)
                val daemonOk = runCatching { OmegaDaemon.start() }.getOrElse { false }
                Log.d(TAG, if (daemonOk) "✅ OmegaDaemon iniciado"
                           else          "⚠️ OmegaDaemon no disponible (modo no-root / JNI ausente)")

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

                // ISO 226: restaurar calibración previa al arrancar
                if (paramStore.loadIso226Calibrated() && omegaBridge.isConnected) {
                    val lp = paramStore.loadIso226ListenPhon()
                    val rp = paramStore.loadIso226RefPhon()
                    launch(Dispatchers.IO) {
                        val result = Iso226Calibrator.applyAll(this@IVANNAApplication, lp, rp, globalEffectManager)
                        Log.i(TAG, "ISO 226 restaurado: ${result.summary}")
                    }
                }

                // FIX (descableado): Todos los sliders/switches persistidos en UI pero nunca
                // inyectados al Engine en el arranque, ahora se inyectan a la perfeccion.
                //
                // FIX (socket nunca reconecta): estaban anidados — restore() y
                // startKeepalive() compartian el MISMO runCatching, asi que si
                // restore() lanzaba (muy plausible: toca sliders/switches de
                // engines que han cambiado de forma en el resto del arbol),
                // la ejecucion nunca llegaba a startKeepalive(). Resultado real:
                // connect() inicial fallaba (daemon aun en su ventana de bind
                // retry, ver ivanna_daemon.cpp) y el loop de reintento cada 5s
                // JAMAS arrancaba -> isConnected se quedaba en false para
                // siempre, y con el todo lo que depende de el (paneles de
                // telemetria incluidos). Se separan para que cada uno aisle
                // su propio fallo, tal como ya hace cada runCatching en el
                // resto de este bloque.
                runCatching { com.ivanna.omega.core.PersistedStateRestorer.restore(this@IVANNAApplication) }
                runCatching { com.ivanna.omega.magisk.OmegaEngineBridge.startKeepalive() }

                // ── PRIMER LANZAMIENTO: preset magistral de entrada ──────────────
                // En el primer uso (o si el usuario nunca guardó preferencias),
                // enviar los parámetros de experiencia óptima al daemon. Esto
                // asegura que desde el primer segundo el audio ya esté calibrado
                // con la curva completa: spatial, harmonic, bass, widener, ISO 226.
                // Después de este envío el daemon publica el snapshot en el SHM
                // y omega_effect.cpp lo aplica en el siguiente frame de audio.
                if (omegaBridge.isConnected && !paramStore.hasAppliedFirstLaunchPreset()) {
                    launch(Dispatchers.IO) {
                        try {
                            // Parámetros perceptuales magistrales
                            MagiskBridge.sendCommand("""{"action":"SET_PERCEPTUAL_STATE","compressor":-5.5,"exciterReduction":0.15,"highCutHz":19500,"spatialWidth":1.55,"loudnessTargetLuFS":-16.0,"harmonicGain":0.78,"antiDolbyIntensity":0.85}""")
                            // Intensidad global
                            MagiskBridge.sendCommand("""{"action":"SET_INTENSITY","intensity":0.92}""")
                            // Route profile: sub-graves + presencia vocal + ensanchamiento
                            MagiskBridge.sendCommand("""{"action":"SET_ROUTE_PROFILE","bassBoostDb":2.5,"dialogBoostDb":1.5,"widenerMult":1.38}""")
                            // Preset adaptativo — se aplica "Spatial" para el arranque
                            MagiskBridge.setPreset("Spatial")
                            paramStore.markFirstLaunchPresetApplied()
                            Log.i(TAG, "✅ Preset magistral de primer lanzamiento aplicado")
                        } catch (e: Exception) {
                            Log.w(TAG, "Preset primer lanzamiento: ${e.message}")
                        }
                    }
                }

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

                com.ivanna.omega.assistant.core.SecureConfigurationManager.initialize(this@IVANNAApplication)
                com.ivanna.omega.assistant.core.DynamicContextEngine.init(this@IVANNAApplication)

                // ── IVANNA OMEGA SUPREME AI CORE ───────────────────────────────
                // NOTA (auditoría): geminiAgent/cognitiveCore/adaptivePresetEngine/
                // dspOrchestrator se creaban aquí como locals y NUNCA se
                // almacenaban en ninguna propiedad accesible (ni companion, ni
                // singleton) — quedaban huérfanos en cuanto este bloque
                // terminaba. El daño real: GeminiOrchestrator (dentro de
                // geminiAgent) arranca un coroutine propio con su PROPIO
                // CoroutineScope (no ligado al ciclo de vida de nada) que
                // llama a la API de Gemini cada 30s para health-checks, PARA
                // SIEMPRE, sin que ninguna pantalla lo consumiera — batería,
                // datos y cuota de API gastados en un agente que nadie usa.
                // El agente real que sí llega al usuario es el que crea
                // IvannaAssistantViewModel.
                //
                // FIX (leak real, mismo patrón que geminiAgent arriba):
                // memoryArchitecture SÍ tenía efecto colateral continuo — su
                // constructor arranca su PROPIO CoroutineScope(SupervisorJob()
                // + Dispatchers.IO) y un init{} que lanza loadFromDisk(). Esta
                // instancia de nivel Application:
                //   1. Nunca se cerraba (sin shutdown() en onTerminate) — scope
                //      huérfano para siempre, igual que el geminiAgent de antes.
                //   2. Competía por el MISMO archivo EncryptedFile en disco
                //      (ivanna_episodic_memory.json / ivanna_semantic_memory.json)
                //      con la instancia REAL que crea IvannaAssistantViewModel —
                //      riesgo de escritura concurrente/corrupción si ambas
                //      persistían a la vez.
                //   3. Su único consumidor era un log de isLoaded — cero valor
                //      funcional real, ningún componente del producto la leía
                //      ni escribía en ella.
                // Se elimina por completo: la memoria real vive donde el
                // usuario realmente interactúa (IvannaAssistantViewModel), y
                // esa instancia ahora cierra su scope correctamente al salir
                // de la pantalla vía IvannaGeminiAgent.shutdown() → memory.shutdown().

                // Iniciar telemetría OEM
                OEMTelemetry.start(this@IVANNAApplication)

                Log.i(TAG, "✅ IVANNA OMEGA SUPREME AI CORE inicializado")
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
                // HARDENING: proveer path correcto según disponibilidad de root.
                // RootAccess detecta si /data/adb es accesible; sino usa filesDir sandbox.
                val profilePath = if (RootAccess.cachedRoot || RootAccess.suBinaryVisible())
                    "/data/adb/ivanna_omega/profile"
                else
                    filesDir.absolutePath + "/profile"
                CloudSyncManager.syncDown(
                      this@IVANNAApplication,
                      UserProfileManager(profilePath)
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
        runCatching { com.ivanna.omega.audio.AudioBackendSelector.stop() }
        globalEffectManager.releaseAll()
        omegaBridge.disconnect()
        OmegaDaemon.stop()
        com.ivanna.omega.audio.AudioRouteManager.stop()
        runCatching { com.ivanna.omega.neuromorphic.IvannaNpeEngine.release() }
        super.onTerminate()
    }
}
