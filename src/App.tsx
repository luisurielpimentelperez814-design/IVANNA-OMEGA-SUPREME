import { RuntimeRealityControls } from './components/RuntimeRealityControls';
import { applyRuntimeControl } from './lib/runtimeControl';
import { useState, lazy, Suspense } from 'react';
import { ApiAccess } from './components/ApiAccess';
import { PRODUCT_VERSION } from './version';
import { Header } from './components/Header';
import { MasterControlPlane } from './components/MasterControlPlane';
const TinyMlClassifierPanel = lazy(() => import('./components/TinyMlClassifierPanel').then(m => ({ default: m.TinyMlClassifierPanel })));
const EvolutionaryEqPanel = lazy(() => import('./components/EvolutionaryEqPanel').then(m => ({ default: m.EvolutionaryEqPanel })));
const SpatialHrtfPanel = lazy(() => import('./components/SpatialHrtfPanel').then(m => ({ default: m.SpatialHrtfPanel })));
const GoldenEarPanel = lazy(() => import('./components/GoldenEarPanel').then(m => ({ default: m.GoldenEarPanel })));
const NeonProfiler = lazy(() => import('./components/NeonProfiler').then(m => ({ default: m.NeonProfiler })));
import { DspParameters, BenchmarkMetrics, TinyMlClassification } from './types';
const Iso226CalibrationPanel = lazy(() => import('./components/Iso226CalibrationPanel').then(m => ({ default: m.Iso226CalibrationPanel })));
const DspPipeline = lazy(() => import('./components/DspPipeline').then(m => ({ default: m.DspPipeline })));
const ParameterControls = lazy(() => import('./components/ParameterControls').then(m => ({ default: m.ParameterControls })));
import { usePersist } from './usePersist';
const IvannaVoicePanel = lazy(() => import('./components/IvannaVoicePanel').then(m => ({ default: m.IvannaVoicePanel })));
const AcousticRealityPanel = lazy(() => import('./components/AcousticRealityPanel').then(m => ({ default: m.AcousticRealityPanel })));
const FotoHolo3DGenerator = lazy(() => import('./components/FotoHolo3DGenerator').then(m => ({ default: m.FotoHolo3DGenerator })));

// Code-splitting: estos dos tabs se cargan bajo demanda.
// CodeExporter arrastra todas las fuentes C++ del DSP (imports ?raw) y
// AudioVisualizer el motor de canvas/Web Audio — diferirlos reduce el chunk
// inicial y acelera el primer render del dashboard.
const AudioVisualizer = lazy(() =>
  import('./components/AudioVisualizer').then((m) => ({ default: m.AudioVisualizer }))
);
const CodeExporter = lazy(() =>
  import('./components/CodeExporter').then((m) => ({ default: m.CodeExporter }))
);

// Fallback mínimo mientras se descarga el chunk diferido.
const TabFallback = () => (
  <div className="flex items-center justify-center py-16 text-[#A8B6CA] font-mono text-xs">
    Cargando módulo...
  </div>
);

const DEFAULT_PARAMS: DspParameters = {
  masterGain: 1.08,
  antiDolbyIntensity: 0.85,
  masterBypass: false,
  goldenEarEnabled: true,
  goldenEarDrive: 1.25,
  goldenEarMix: 0.18,
  fatigueIndex: 0.12,
  iirAlpha: 0.94,
  crosstalkGain: 0.28,
  hrtfDelayMs: 0.28,
  spatialAngleDeg: 32,
  spatialWidth: 1.32,
  spatialWetEta: 0.42,
  eqMutationRate: 0.08,
  sampleRate: 96000,
  blockSize: 512,
  nhoAlpha: 0.90,
  nhoBeta: 0.85,
  harmonicGain: 1.15,
  hrtfEnabled: true,
  adaptEnabled: true,
  compThresholdDb: -16.0,
  compRatio: 2.4,
  compAttackMs: 1.8,
  compReleaseMs: 110,
  activePreset: 'anti_dolby_extreme',
};

const DEFAULT_METRICS: BenchmarkMetrics = {
  blockLatencyMicroseconds: 12.4,
  sampleLatencyNanoseconds: 24.2,
  simdEfficiencyPercent: 100,
  heapAllocationsInAudioThread: 0,
  l1CacheHitRatePercent: 99.98,
  gflopsThroughput: 148.5,
  registersActiveCount: 32,
  clipCount: 0,
  evolutionFitness: -0.0182,
  optimalBlockSize: 512,
  isCalibrating: false,
  calibrationProgress: 0,
  calibrationLog: [],
  lastCalibratedAt: undefined,
};

type ActiveTab = 'master' | 'reality_hyperengine' | 'tinyml' | 'evo_eq' | 'spatial' | 'golden_ear' | 'visualizer' | 'benchmarks' | 'code' | 'iso226' | 'pipeline' | 'ivanna_voice';

export default function App() {
  // ── Persistencia de tab activo ──────────────────────────────────────────────
  const [activeTab, setActiveTab] = usePersist<ActiveTab>('activeTab', 'master');

  // ── Persistencia completa de parámetros DSP ─────────────────────────────────
  const [params, setParams] = usePersist<DspParameters>('params', DEFAULT_PARAMS);

  // Demonstration is opt-in and never persisted as device state.
  const [demo, setDemo] = useState(false);
  const [controlStatus, setControlStatus] = useState("Ningún control enviado al motor.");
  const metrics = DEFAULT_METRICS;
  const classification: TinyMlClassification = {
    speech: 0.42, music: 0.38, transient: 0.12, ambient: 0.08,
    dominantClass: 'Ejemplo: voz', spscDepth: 512, inferenceTimeUs: 8.2,
  };
  const unavailable = <section role="status" className="rounded-xl border border-slate-600 p-6 text-slate-200">
    Sin medición nativa para este panel. Abre el diagnóstico Android para medir el motor,
    o activa la demostración para explorar datos de ejemplo.
  </section>;

  // ── Handlers ────────────────────────────────────────────────────────────────
  const handleParamChange = <K extends keyof DspParameters>(key: K, value: DspParameters[K]) => {
    setParams((prev) => ({ ...prev, [key]: value, activePreset: 'custom' }));
    if (demo) { setControlStatus('Demostración: configuración local; no se envían comandos.'); return; }
    const bridge = window.IvannaFotoHoloBridge;
    setControlStatus(applyRuntimeControl(bridge?.applyRuntimeControl ? {
      applyRuntimeControl: (name, next) => bridge.applyRuntimeControl!(name, next),
    } : undefined, key, value).message);
  };

  const handleApplyPreset = (presetName: DspParameters['activePreset']) => {
    const presets: Record<string, Partial<DspParameters>> = {
      anti_dolby_extreme: {
        antiDolbyIntensity: 1.0, masterGain: 1.15, goldenEarEnabled: true,
        goldenEarDrive: 1.35, goldenEarMix: 0.20, crosstalkGain: 0.25,
        hrtfEnabled: true, blockSize: 512,
      },
      audiophile: {
        antiDolbyIntensity: 0.70, masterGain: 1.0, goldenEarEnabled: true,
        goldenEarDrive: 1.10, goldenEarMix: 0.10, spatialAngleDeg: 45,
        spatialWidth: 1.35, crosstalkGain: 0.35, hrtfEnabled: true, blockSize: 512,
      },
      bass_head: {
        antiDolbyIntensity: 0.80, masterGain: 1.10, goldenEarEnabled: true,
        goldenEarDrive: 2.10, goldenEarMix: 0.30, harmonicGain: 1.5, blockSize: 1024,
      },
      vocal_protect: {
        antiDolbyIntensity: 0.90, masterGain: 1.0, fatigueIndex: 0.25,
        iirAlpha: 0.90, compRatio: 3.5, blockSize: 512,
      },
      gaming_spatial: {
        antiDolbyIntensity: 0.95, masterGain: 1.05, spatialAngleDeg: 60,
        spatialWidth: 1.50, hrtfEnabled: true, blockSize: 256,
      },
      evo_cma_es: {
        antiDolbyIntensity: 0.88, eqMutationRate: 0.22, goldenEarEnabled: true,
        goldenEarDrive: 1.40, blockSize: 512,
      },
    };

    setControlStatus("Preset guardado localmente. Ajusta los controles compatibles individualmente para enviarlos al motor.");
    if (presetName in presets) {
      setParams((prev) => ({ ...prev, ...presets[presetName], activePreset: presetName }));
    } else {
      setParams((prev) => ({ ...prev, activePreset: 'custom' }));
    }
  };

  return (
    <div className="min-h-screen bg-[#0A0C10] text-[#E2E8F0] font-sans selection:bg-[#38BDF8] selection:text-[#0A0C10]">
      <Header
        activeTab={activeTab}
        setActiveTab={setActiveTab}
        params={params}
        onParamChange={handleParamChange}
        onApplyPreset={handleApplyPreset}
      />

      <section className="mx-auto max-w-7xl px-4 pt-4 space-y-3" aria-label="Origen de datos y acceso">
        <div className="rounded-xl border border-amber-300/40 bg-slate-950 p-4 text-sm text-slate-200">
          <strong>{demo ? 'DEMOSTRACIÓN · DATOS DE EJEMPLO' : 'CONFIGURACIÓN LOCAL · SIN TELEMETRÍA DSP GLOBAL'}</strong>
          <p>Anchura, ángulo e intensidad Anti-Dolby pueden enviarse mediante el puente Android con lectura de retorno. Los demás controles preparan una configuración local. FotoHolo informa por separado
          si su puente Android está conectado. Los gráficos de diseño no son mediciones acústicas.</p>
          <p role="status" className="mt-2">Último cambio: {controlStatus}</p>
          <label className="mt-3 flex items-center gap-2"><input type="checkbox" checked={demo}
            onChange={e => setDemo(e.target.checked)} /> Mostrar demostraciones con datos sintéticos</label>
        </div>
        <ApiAccess />
      </section>
      <main className="max-w-7xl mx-auto px-4 sm:px-6 lg:px-8 py-8">
      <Suspense fallback={<TabFallback />}>
        {activeTab === 'master' && (
          <MasterControlPlane
            params={params}
            metrics={metrics}
            onParamChange={handleParamChange}
            demo={demo}
            onApplyPreset={handleApplyPreset}
          />
        )}
        {activeTab === 'reality_hyperengine' && (
          <div className="space-y-6">
            <FotoHolo3DGenerator allowNative={!demo} params={params} onParamChange={handleParamChange} />
            {!demo && <RuntimeRealityControls />}
            {demo && <AcousticRealityPanel params={params} onParamChange={handleParamChange} />}
          </div>
        )}
        {activeTab === 'tinyml' && (demo ?
          <TinyMlClassifierPanel params={params} classification={classification} onParamChange={handleParamChange} /> : unavailable
        )}
        {activeTab === 'evo_eq' && (demo ?
          <EvolutionaryEqPanel params={params} metrics={metrics} onParamChange={handleParamChange} /> : unavailable
        )}
        {activeTab === 'spatial' && (
          <SpatialHrtfPanel params={params} onParamChange={handleParamChange} />
        )}
        {activeTab === 'golden_ear' && (
          <GoldenEarPanel params={params} onParamChange={handleParamChange} />
        )}
        {activeTab === 'visualizer' && (demo ?
          <Suspense fallback={<TabFallback />}>
            <AudioVisualizer params={params} />
          </Suspense> : unavailable
        )}
        {activeTab === 'benchmarks' && (demo ? <NeonProfiler metrics={metrics} /> : unavailable)}
        {activeTab === 'code' && (
          <Suspense fallback={<TabFallback />}>
            <CodeExporter />
          </Suspense>
        )}
        {activeTab === 'iso226' && (
          <Iso226CalibrationPanel params={params} onParamChange={handleParamChange} />
        )}
        {activeTab === 'pipeline' && (
          <div className="space-y-6">
            <DspPipeline params={params} />
            <ParameterControls params={params} onParamChange={handleParamChange} />
          </div>
        )}
        {activeTab === 'ivanna_voice' && (
          <IvannaVoicePanel />
        )}
      </Suspense>
      </main>

      <footer className="border-t border-[#1E2330] bg-[#0A0C10]/90 py-6 mt-12 font-mono text-xs text-[#A8B6CA]">
        <div className="max-w-7xl mx-auto px-4 sm:px-6 lg:px-8 flex flex-col sm:flex-row items-center justify-between gap-4">
          <div>
            <span>IVANNA-OMEGA-SUPREME v{PRODUCT_VERSION} · Dashboard de configuración</span>
          </div>
          <div className="flex items-center space-x-4">
            <span className="text-[#4ADE80]">● Estado local</span>
            <span className="text-[#38BDF8]">● Medición: Android</span>
            <span className="text-[#F59E0B]">● Capacidades según dispositivo</span>
          </div>
        </div>
      </footer>
    </div>
  );
}
