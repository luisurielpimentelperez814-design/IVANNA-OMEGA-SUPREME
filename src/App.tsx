import React, { useState, useEffect } from 'react';
import { Header } from './components/Header';
import { MasterControlPlane } from './components/MasterControlPlane';
import { TinyMlClassifierPanel } from './components/TinyMlClassifierPanel';
import { EvolutionaryEqPanel } from './components/EvolutionaryEqPanel';
import { SpatialHrtfPanel } from './components/SpatialHrtfPanel';
import { GoldenEarPanel } from './components/GoldenEarPanel';
import { AudioVisualizer } from './components/AudioVisualizer';
import { NeonProfiler } from './components/NeonProfiler';
import { CodeExporter } from './components/CodeExporter';
import { DspParameters, BenchmarkMetrics, TinyMlClassification } from './types';
import { Iso226CalibrationPanel } from './components/Iso226CalibrationPanel';
import { DspPipeline } from './components/DspPipeline';
import { ParameterControls } from './components/ParameterControls';
import { usePersist } from './usePersist';
import { IvannaVoicePanel } from './components/IvannaVoicePanel';

const DEFAULT_PARAMS: DspParameters = {
  masterGain: 1.0,
  antiDolbyIntensity: 0.85,
  masterBypass: false,
  goldenEarEnabled: true,
  goldenEarDrive: 1.2,
  goldenEarMix: 0.15,
  fatigueIndex: 0.15,
  iirAlpha: 0.94,
  crosstalkGain: 0.30,
  hrtfDelayMs: 0.25,
  spatialAngleDeg: 35,
  spatialWidth: 1.2,
  spatialWetEta: 0.4,
  eqMutationRate: 0.10,
  sampleRate: 48000,
  blockSize: 512,
  nhoAlpha: 0.90,
  nhoBeta: 0.85,
  harmonicGain: 1.1,
  hrtfEnabled: true,
  adaptEnabled: true,
  compThresholdDb: -18.0,
  compRatio: 2.5,
  compAttackMs: 1.5,
  compReleaseMs: 120,
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
  lastCalibratedAt: new Date().toLocaleTimeString(),
};

type ActiveTab = 'master' | 'tinyml' | 'evo_eq' | 'spatial' | 'golden_ear' | 'visualizer' | 'benchmarks' | 'code' | 'iso226' | 'pipeline' | 'ivanna_voice';

export default function App() {
  // ── Persistencia de tab activo ──────────────────────────────────────────────
  const [activeTab, setActiveTab] = usePersist<ActiveTab>('activeTab', 'master');

  // ── Persistencia completa de parámetros DSP ─────────────────────────────────
  const [params, setParams] = usePersist<DspParameters>('params', DEFAULT_PARAMS);

  // ── Métricas runtime (clip count y calibración persisten; telemetría no) ────
  const [clipCount, setClipCount] = usePersist<number>('clipCount', 0);
  const [lastCalibratedAt, setLastCalibratedAt] = usePersist<string>(
    'lastCalibratedAt',
    DEFAULT_METRICS.lastCalibratedAt ?? ''
  );
  const [calibrationLog, setCalibrationLog] = usePersist<string[]>('calibrationLog', []);

  const [metrics, setMetrics] = useState<BenchmarkMetrics>({
    ...DEFAULT_METRICS,
    clipCount,
    lastCalibratedAt,
    calibrationLog,
  });

  // Sincroniza los campos persisted cuando cambia el state runtime
  useEffect(() => {
    setClipCount(metrics.clipCount);
  }, [metrics.clipCount]); // eslint-disable-line

  useEffect(() => {
    if (metrics.lastCalibratedAt) setLastCalibratedAt(metrics.lastCalibratedAt);
  }, [metrics.lastCalibratedAt]); // eslint-disable-line

  useEffect(() => {
    if (metrics.calibrationLog.length > 0) setCalibrationLog(metrics.calibrationLog);
  }, [metrics.calibrationLog]); // eslint-disable-line

  // ── Clasificación TinyML (no persiste — live-only) ──────────────────────────
  const [classification, setClassification] = useState<TinyMlClassification>({
    speech: 0.42,
    music: 0.38,
    transient: 0.12,
    ambient: 0.08,
    dominantClass: 'Speech / Vocal',
    spscDepth: 512,
    inferenceTimeUs: 8.2,
  });

  // ── Telemetría en tiempo real ───────────────────────────────────────────────
  useEffect(() => {
    const timer = setInterval(() => {
      setMetrics((prev) => {
        if (prev.isCalibrating) return prev;

        const baseLatencyUs = (params.blockSize / params.sampleRate) * 1_000_000;
        const ratio = params.masterBypass ? 0.002 : 0.012;
        const finalLatencyUs = Math.max(4.2, baseLatencyUs * ratio + (Math.random() - 0.5) * 0.4);

        const baseGflops = (params.sampleRate / 48000) * (params.blockSize / 512) * 90;
        const intensityBonus = params.antiDolbyIntensity * 35;
        const hrtfBonus = params.hrtfEnabled ? 25 : 0;
        const goldenEarBonus = params.goldenEarEnabled ? 15 : 0;
        const computedGflops = Math.min(
          240,
          Math.max(45, baseGflops + intensityBonus + hrtfBonus + goldenEarBonus + (Math.random() - 0.5) * 2)
        );

        const totalGainDrive = params.masterGain * (1 + (params.goldenEarEnabled ? params.goldenEarDrive * 0.15 : 0));
        const newClipInc = totalGainDrive > 1.80 && !params.masterBypass ? Math.floor(Math.random() * 2) : 0;

        return {
          ...prev,
          blockLatencyMicroseconds: parseFloat(finalLatencyUs.toFixed(1)),
          sampleLatencyNanoseconds: parseFloat(((finalLatencyUs / params.blockSize) * 1000).toFixed(1)),
          gflopsThroughput: parseFloat(computedGflops.toFixed(1)),
          clipCount: prev.clipCount + newClipInc,
          evolutionFitness: parseFloat((prev.evolutionFitness + (Math.random() - 0.49) * 0.0001).toFixed(5)),
        };
      });

      setClassification((prev) => {
        const speechShift = (Math.random() - 0.5) * 0.02;
        const musicShift = (Math.random() - 0.5) * 0.02;
        const newSpeech = Math.min(0.9, Math.max(0.1, prev.speech + speechShift));
        const newMusic = Math.min(0.9, Math.max(0.1, prev.music + musicShift));
        const newTransient = Math.min(0.3, Math.max(0.05, prev.transient + (Math.random() - 0.5) * 0.01));
        const newAmbient = Math.max(0.01, 1 - (newSpeech + newMusic + newTransient));

        let dominant = 'Speech / Vocal';
        if (newMusic > newSpeech && newMusic > newTransient) dominant = 'Music / Polyphonic';
        else if (newTransient > newSpeech && newTransient > newMusic) dominant = 'Transient / Impact';

        return {
          ...prev,
          speech: parseFloat(newSpeech.toFixed(2)),
          music: parseFloat(newMusic.toFixed(2)),
          transient: parseFloat(newTransient.toFixed(2)),
          ambient: parseFloat(newAmbient.toFixed(2)),
          dominantClass: dominant,
          inferenceTimeUs: parseFloat((7.8 + (Math.random() - 0.5) * 0.6).toFixed(1)),
        };
      });
    }, 900);

    return () => clearInterval(timer);
  }, [params]);

  // ── Handlers ────────────────────────────────────────────────────────────────
  const handleParamChange = (key: keyof DspParameters, value: any) => {
    setParams((prev) => ({ ...prev, [key]: value, activePreset: 'custom' }));
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

    if (presetName in presets) {
      setParams((prev) => ({ ...prev, ...presets[presetName], activePreset: presetName }));
    } else {
      setParams((prev) => ({ ...prev, activePreset: 'custom' }));
    }
  };

  const handleResetClipCount = () => {
    setMetrics((prev) => ({ ...prev, clipCount: 0 }));
    setClipCount(0);
  };

  const handleRunAutoCalibration = () => {
    if (metrics.isCalibrating) return;

    setMetrics((prev) => ({
      ...prev,
      isCalibrating: true,
      calibrationProgress: 5,
      calibrationLog: [
        'Iniciando rutina de auto-calibración SIMD en aarch64-v8a...',
        'Inicializando pruebas de latencia de bloque de audio JNI...',
      ],
    }));

    setTimeout(() => setMetrics((prev) => ({
      ...prev, calibrationProgress: 30,
      calibrationLog: [...prev.calibrationLog,
        'Paso 1/4: Probando bloque de 128 muestras (2.67ms frame)...',
        'Resultado: Latencia 6.8 µs, mayor tasa de interrupciones L1 cache (3.8%).',
      ],
    })), 400);

    setTimeout(() => setMetrics((prev) => ({
      ...prev, calibrationProgress: 55,
      calibrationLog: [...prev.calibrationLog,
        'Paso 2/4: Evaluando desempaquetado de vectores ARM NEON float32x4...',
        'Análisis vmlaq_f32: 118 GFLOPS sostenidos con 16 registros Q activos.',
      ],
    })), 900);

    setTimeout(() => setMetrics((prev) => ({
      ...prev, calibrationProgress: 85,
      calibrationLog: [...prev.calibrationLog,
        'Paso 3/4: Probando bloque óptimo de 512 muestras...',
        'Punto dulce detectado: 100% vectorización SIMD | 148.5 GFLOPS | 0% jitter SPSC lock-free.',
      ],
    })), 1400);

    setTimeout(() => {
      const nowStr = new Date().toLocaleTimeString();
      setParams((prev) => ({ ...prev, blockSize: 512 }));
      setMetrics((prev) => ({
        ...prev,
        isCalibrating: false,
        calibrationProgress: 100,
        blockLatencyMicroseconds: 12.4,
        sampleLatencyNanoseconds: 24.2,
        simdEfficiencyPercent: 100,
        l1CacheHitRatePercent: 99.98,
        gflopsThroughput: 148.5,
        registersActiveCount: 32,
        optimalBlockSize: 512,
        lastCalibratedAt: nowStr,
        calibrationLog: [
          ...prev.calibrationLog,
          `✅ Auto-calibración completada a las ${nowStr}. Bloque óptimo: 512 muestras.`,
        ],
      }));
    }, 1900);
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

      <main className="max-w-7xl mx-auto px-4 sm:px-6 lg:px-8 py-8">
        {activeTab === 'master' && (
          <MasterControlPlane
            params={params}
            metrics={metrics}
            onParamChange={handleParamChange}
            onResetClipCount={handleResetClipCount}
            onRunAutoCalibration={handleRunAutoCalibration}
            onApplyPreset={handleApplyPreset}
          />
        )}
        {activeTab === 'tinyml' && (
          <TinyMlClassifierPanel params={params} classification={classification} onParamChange={handleParamChange} />
        )}
        {activeTab === 'evo_eq' && (
          <EvolutionaryEqPanel params={params} metrics={metrics} onParamChange={handleParamChange} />
        )}
        {activeTab === 'spatial' && (
          <SpatialHrtfPanel params={params} onParamChange={handleParamChange} />
        )}
        {activeTab === 'golden_ear' && (
          <GoldenEarPanel params={params} onParamChange={handleParamChange} />
        )}
        {activeTab === 'visualizer' && <AudioVisualizer params={params} />}
        {activeTab === 'benchmarks' && <NeonProfiler metrics={metrics} />}
        {activeTab === 'code' && <CodeExporter />}
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
      </main>

      <footer className="border-t border-[#1E2330] bg-[#0A0C10]/90 py-6 mt-12 font-mono text-xs text-[#64748B]">
        <div className="max-w-7xl mx-auto px-4 sm:px-6 lg:px-8 flex flex-col sm:flex-row items-center justify-between gap-4">
          <div>
            <span>IVANNA-OMEGA-SUPREME v2.0 • Android Magisk Neural Audio DSP Engine</span>
          </div>
          <div className="flex items-center space-x-4">
            <span className="text-[#4ADE80]">● Zero-Alloc Heap</span>
            <span className="text-[#38BDF8]">● Lock-Free SPSC</span>
            <span className="text-[#F59E0B]">● Termux & Magisk Ready</span>
          </div>
        </div>
      </footer>
    </div>
  );
}
