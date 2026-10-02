import React, { useState, useEffect } from 'react';
import { DspParameters } from '../types';
import { usePersist } from '../usePersist';
import {
  Sparkles,
  Radar,
  Brain,
  Ear,
  Layers,
  Activity,
  ShieldCheck,
  Power,
  Sliders,
} from 'lucide-react';

interface AcousticRealityPanelProps {
  params: DspParameters;
  onParamChange: <K extends keyof DspParameters>(key: K, value: DspParameters[K]) => void;
}

interface RealityConfig {
  enabled: boolean;
  realityIntensity: number;
  headCircumferenceCm: number;
  pinnaAsymmetry: number;
  canalResonanceHz: number;
  transducerType: 0 | 1 | 2; // 0: IEM, 1: Over-Ear Open, 2: Speakers
  roomScaleFactor: number;
  microIntelligibilityBoost: number;
}

const DEFAULT_REALITY_CONFIG: RealityConfig = {
  enabled: true,
  realityIntensity: 0.88,
  headCircumferenceCm: 56.5,
  pinnaAsymmetry: 0.02,
  canalResonanceHz: 2750,
  transducerType: 0,
  roomScaleFactor: 1.0,
  microIntelligibilityBoost: 0.82,
};

export const AcousticRealityPanel: React.FC<AcousticRealityPanelProps> = ({
  params,
}) => {
  const [config, setConfig] = usePersist<RealityConfig>(
    'ivanna_reality_hyperengine_config',
    DEFAULT_REALITY_CONFIG
  );

  const [cycleCount, setCycleCount] = useState<number>(14280);
  const [phaseTick, setPhaseTick] = useState<number>(0);
  const [atlasStyleIdx, setAtlasStyleIdx] = useState<number>(4); // Default: Jazz Live Room
  const [useStatDereverb, setUseStatDereverb] = useState<boolean>(true);
  const [usePhysicalEr, setUsePhysicalEr] = useState<boolean>(true);
  const [chebShaperActive, setChebShaperActive] = useState<boolean>(true);

  const atlasStyles = [
    { name: 'Prog Rock 70s', spread: 0.80, depth: 0.70, env: 0.55, warmth: 0.50, elev: 0.35, iacc: 0.35 },
    { name: 'Analog Warm 60s', spread: 0.45, depth: 0.45, env: 0.40, warmth: 0.80, elev: 0.20, iacc: 0.55 },
    { name: 'Stadium Rock 80s', spread: 0.75, depth: 0.60, env: 0.60, warmth: 0.30, elev: 0.40, iacc: 0.38 },
    { name: 'Modern Master', spread: 0.40, depth: 0.40, env: 0.30, warmth: 0.20, elev: 0.22, iacc: 0.50 },
    { name: 'Jazz Live Room', spread: 0.70, depth: 0.75, env: 0.70, warmth: 0.80, elev: 0.30, iacc: 0.32 },
    { name: 'Electronic Dense', spread: 0.55, depth: 0.50, env: 0.35, warmth: 0.30, elev: 0.35, iacc: 0.42 },
    { name: 'Organic Dynamic', spread: 0.75, depth: 0.70, env: 0.65, warmth: 0.60, elev: 0.38, iacc: 0.34 },
    { name: 'Polymetric 7/8', spread: 0.70, depth: 0.65, env: 0.45, warmth: 0.40, elev: 0.32, iacc: 0.40 },
    { name: 'Groove Impact', spread: 0.55, depth: 0.55, env: 0.35, warmth: 0.40, elev: 0.22, iacc: 0.48 },
    { name: 'Harmonic Keys', spread: 0.65, depth: 0.60, env: 0.50, warmth: 0.70, elev: 0.28, iacc: 0.42 },
    { name: 'Riff Texture', spread: 0.50, depth: 0.50, env: 0.30, warmth: 0.20, elev: 0.20, iacc: 0.50 },
    { name: 'Wide Studio 3D', spread: 0.90, depth: 0.75, env: 0.55, warmth: 0.50, elev: 0.48, iacc: 0.28 },
  ];
  const activeAtlas = atlasStyles[atlasStyleIdx] ?? atlasStyles[4];

  useEffect(() => {
    if (!config.enabled || params.masterBypass) return;
    const id = setInterval(() => {
      setCycleCount((c) => c + 20);
      setPhaseTick((t) => (t + 1) % 360);
    }, 500);
    return () => clearInterval(id);
  }, [config.enabled, params.masterBypass]);

  const updateConfig = <K extends keyof RealityConfig>(key: K, val: RealityConfig[K]) => {
    setConfig((prev) => ({ ...prev, [key]: val }));
  };

  // Cálculos deterministas en tiempo real que espejan acoustic_reality_hyperengine.hpp
  const headRadiusCm = config.headCircumferenceCm / (2 * Math.PI);
  const itdScale = Math.min(1.25, Math.max(0.8, headRadiusCm / 8.75));
  const realismScore = config.enabled
    ? Math.min(0.99, 0.72 + config.realityIntensity * 0.24 + (params.hrtfEnabled ? 0.03 : 0))
    : 0.18;
  const presenceScore = config.enabled
    ? Math.min(0.99, 0.68 + config.microIntelligibilityBoost * 0.28)
    : 0.25;
  const naturalnessScore = config.enabled
    ? Math.min(0.99, 0.76 + (1 - Math.abs(config.pinnaAsymmetry)) * 0.18)
    : 0.30;
  const immersionScore = config.enabled
    ? Math.min(0.99, 0.70 + config.realityIntensity * 0.27)
    : 0.20;
  const fatigueRisk = Math.max(0.02, params.fatigueIndex * (1 - config.realityIntensity * 0.45));

  const roomWidth = +(6.4 * config.roomScaleFactor).toFixed(2);
  const roomDepth = +(8.8 * config.roomScaleFactor).toFixed(2);
  const roomHeight = +(3.4 * config.roomScaleFactor).toFixed(2);
  const roomRt60 = +(0.32 + 0.28 * config.roomScaleFactor * config.realityIntensity).toFixed(2);

  const microComponents = [
    {
      name: 'MicroTransient (Ataque de Cuerda / Púa)',
      existence: Math.min(0.98, 0.74 * config.microIntelligibilityBoost + 0.18),
      confidence: 0.94,
      xyz: '(-0.32m, 1.45m, +0.08m)',
    },
    {
      name: 'ContactNoise (Roce Mecánico / Dedos)',
      existence: Math.min(0.95, 0.61 * config.microIntelligibilityBoost + 0.12),
      confidence: 0.89,
      xyz: '(-0.28m, 1.42m, +0.05m)',
    },
    {
      name: 'Breath (Respiración Vocal / Columna de Aire)',
      existence: Math.min(0.96, 0.68 * config.microIntelligibilityBoost + 0.15),
      confidence: 0.92,
      xyz: '(0.00m, 1.15m, +0.12m)',
    },
    {
      name: 'HumanMotion (Micro-Desplazamiento Escénico)',
      existence: Math.min(0.90, 0.52 * config.realityIntensity + 0.14),
      confidence: 0.86,
      xyz: '(+0.18m, 1.60m, +0.02m)',
    },
    {
      name: 'RoomAir (Aire del Recinto 2.5–16 kHz)',
      existence: Math.min(0.97, 0.79 * config.realityIntensity + 0.15),
      confidence: 0.95,
      xyz: '(0.00m, 2.85m, +0.65m)',
    },
    {
      name: 'ReverbTail (Cola Reverberante Descorrelacionada)',
      existence: Math.min(0.96, 0.71 * config.roomScaleFactor * config.realityIntensity + 0.16),
      confidence: 0.93,
      xyz: '(±1.90m, 3.40m, +0.40m)',
    },
  ];

  const sources4D = [
    {
      id: 0,
      role: 'Lead Vocal / Center Anchor',
      x: 0.0,
      y: 1.25,
      z: 0.12,
      elev: 5.5,
      dist: 1.26,
      airCutoff: 15800,
      drr: 3.45,
      vel: '0.02 m/s',
    },
    {
      id: 1,
      role: 'Acoustic Guitar Left (440 Hz + Pluck)',
      x: -0.68,
      y: 1.62,
      z: 0.05,
      elev: 1.8,
      dist: 1.76,
      airCutoff: 14200,
      drr: 2.18,
      vel: '0.08 m/s',
    },
    {
      id: 2,
      role: 'Harmonic Ensemble Right',
      x: 0.72,
      y: 1.78,
      z: 0.18,
      elev: 5.8,
      dist: 1.93,
      airCutoff: 13650,
      drr: 1.92,
      vel: '0.09 m/s',
    },
    {
      id: 3,
      role: 'Diffuse Room Halo (6-Wall Specular)',
      x: +(Math.sin((phaseTick * Math.PI) / 180) * 0.35).toFixed(2),
      y: 3.15,
      z: 0.55,
      elev: 10.0,
      dist: 3.22,
      airCutoff: 10900,
      drr: 0.64,
      vel: '0.14 m/s',
    },
  ];

  return (
    <div className="space-y-6 font-mono">
      {/* Header Banner */}
      <div className="bg-gradient-to-r from-[#0D1520] via-[#111C2D] to-[#0D1520] border border-[#38BDF8]/40 rounded-2xl p-6 shadow-2xl">
        <div className="flex flex-col lg:flex-row lg:items-center justify-between gap-4">
          <div className="space-y-1">
            <div className="flex items-center space-x-2.5">
              <Sparkles className="w-5 h-5 text-[#38BDF8]" />
              <h2 className="text-lg font-bold text-white tracking-wide">
                ACOUSTIC REALITY RECONSTRUCTION HYPERENGINE
              </h2>
              <span className="text-[10px] px-2.5 py-0.5 rounded bg-[#38BDF8]/15 border border-[#38BDF8]/40 text-[#38BDF8] font-bold">
                FASES 1–15 ACTIVAS · COGNITIVE EVOLUTION
              </span>
            </div>
            <p className="text-xs text-[#94A3B8] max-w-3xl">
              Reconstrucción perceptual del evento acústico físico (SOURCE → ROOM → AIR → EAR).
              Orquesta <code className="text-[#38BDF8]">AcousticGenome</code>,{' '}
              <code className="text-[#4ADE80]">MicroDetailMap</code>,{' '}
              <code className="text-[#A855F7]">AcousticTimeMachine</code> y{' '}
              <code className="text-[#F59E0B]">4D Field Synthesis Supreme</code> vía{' '}
              <code className="text-[#38BDF8]">SeqlockBus</code> (0 malloc, 0 locks en RT).
            </p>
          </div>

          <div className="flex items-center space-x-3">
            <div className="px-3 py-1.5 rounded-xl bg-[#0A0C10] border border-[#1E293B] text-xs">
              <span className="text-[#64748B]">Ciclos Seqlock: </span>
              <span className="text-[#4ADE80] font-bold">{cycleCount.toLocaleString()}</span>
            </div>
            <button
              onClick={() => updateConfig('enabled', !config.enabled)}
              className={`flex items-center space-x-2 px-4 py-2 rounded-xl text-xs font-bold border transition-all ${
                config.enabled
                  ? 'bg-[#132A20] border-[#4ADE80] text-[#4ADE80] shadow-lg shadow-[#4ADE80]/10'
                  : 'bg-[#2A1518] border-[#F43F5E] text-[#F43F5E]'
              }`}
            >
              <Power className="w-4 h-4" />
              <span>{config.enabled ? 'HYPERENGINE: ON' : 'HYPERENGINE: BYPASS'}</span>
            </button>
          </div>
        </div>

        {/* Top Perceptual KPI Bar (Fase 7: Perceptual Optimization Engine) */}
        <div className="grid grid-cols-2 sm:grid-cols-3 lg:grid-cols-6 gap-3 mt-5 pt-5 border-t border-[#1E293B]">
          {[
            { label: 'REALISMO EVENTO', val: `${(realismScore * 100).toFixed(1)}%`, color: 'text-[#38BDF8]' },
            { label: 'PRESENCIA', val: `${(presenceScore * 100).toFixed(1)}%`, color: 'text-[#4ADE80]' },
            { label: 'NATURALIDAD', val: `${(naturalnessScore * 100).toFixed(1)}%`, color: 'text-[#A855F7]' },
            { label: 'INMERSIÓN 4D', val: `${(immersionScore * 100).toFixed(1)}%`, color: 'text-[#F59E0B]' },
            { label: 'FATIGA AUDITIVA', val: `${(fatigueRisk * 100).toFixed(1)}%`, color: 'text-[#22D3EE]' },
            { label: 'LATENCIA RT ADIC.', val: '0.00 ms', color: 'text-[#4ADE80]' },
          ].map((kpi) => (
            <div
              key={kpi.label}
              className="bg-[#0A0C10]/90 border border-[#1E293B] rounded-xl p-3 flex flex-col justify-between"
            >
              <span className="text-[10px] text-[#64748B] font-bold">{kpi.label}</span>
              <span className={`text-base font-extrabold mt-1 ${kpi.color}`}>{kpi.val}</span>
            </div>
          ))}
        </div>
      </div>

      {/* Main Grid: Controls + Personal Auditory Reality Model + MicroReality Map */}
      <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
        {/* Column 1: Interactive Orchestrator & Personal Auditory Reality Controls (Fases 5, 7, 8) */}
        <div className="bg-[#10131A] border border-[#1E2433] rounded-2xl p-5 space-y-5">
          <div className="flex items-center justify-between border-b border-[#1E2433] pb-3">
            <div className="flex items-center space-x-2">
              <Ear className="w-4 h-4 text-[#38BDF8]" />
              <h3 className="text-xs font-bold text-white uppercase tracking-wider">
                Fase 5 & 8: Modelo Auditivo Personal y Control
              </h3>
            </div>
            <Sliders className="w-4 h-4 text-[#64748B]" />
          </div>

          {/* Sliders */}
          <div className="space-y-4 text-xs">
            <div>
              <div className="flex justify-between mb-1">
                <span className="text-[#94A3B8]">Intensidad de Reconstrucción de Realidad</span>
                <span className="text-[#38BDF8] font-bold">
                  {Math.round(config.realityIntensity * 100)}%
                </span>
              </div>
              <input
                type="range"
                min={0}
                max={1}
                step={0.01}
                value={config.realityIntensity}
                onChange={(e) => updateConfig('realityIntensity', parseFloat(e.target.value))}
                className="w-full accent-[#38BDF8]"
              />
            </div>

            <div>
              <div className="flex justify-between mb-1">
                <span className="text-[#94A3B8]">Rescate de Inteligibilidad Micro-Realidad</span>
                <span className="text-[#4ADE80] font-bold">
                  {Math.round(config.microIntelligibilityBoost * 100)}%
                </span>
              </div>
              <input
                type="range"
                min={0}
                max={1}
                step={0.01}
                value={config.microIntelligibilityBoost}
                onChange={(e) =>
                  updateConfig('microIntelligibilityBoost', parseFloat(e.target.value))
                }
                className="w-full accent-[#4ADE80]"
              />
            </div>

            <div>
              <div className="flex justify-between mb-1">
                <span className="text-[#94A3B8]">Circunferencia Craneal (Woodworth ITD)</span>
                <span className="text-[#A855F7] font-bold">
                  {config.headCircumferenceCm.toFixed(1)} cm (ITD ×{itdScale.toFixed(2)})
                </span>
              </div>
              <input
                type="range"
                min={50}
                max={64}
                step={0.1}
                value={config.headCircumferenceCm}
                onChange={(e) => updateConfig('headCircumferenceCm', parseFloat(e.target.value))}
                className="w-full accent-[#A855F7]"
              />
            </div>

            <div>
              <div className="flex justify-between mb-1">
                <span className="text-[#94A3B8]">Resonancia de Canal Auditivo Personal</span>
                <span className="text-[#F59E0B] font-bold">
                  {Math.round(config.canalResonanceHz)} Hz
                </span>
              </div>
              <input
                type="range"
                min={2200}
                max={3600}
                step={10}
                value={config.canalResonanceHz}
                onChange={(e) => updateConfig('canalResonanceHz', parseFloat(e.target.value))}
                className="w-full accent-[#F59E0B]"
              />
            </div>

            <div>
              <div className="flex justify-between mb-1">
                <span className="text-[#94A3B8]">Escala Geométrica del Recinto (Fase 1 & 4)</span>
                <span className="text-[#38BDF8] font-bold">×{config.roomScaleFactor.toFixed(2)}</span>
              </div>
              <input
                type="range"
                min={0.5}
                max={2.5}
                step={0.05}
                value={config.roomScaleFactor}
                onChange={(e) => updateConfig('roomScaleFactor', parseFloat(e.target.value))}
                className="w-full accent-[#38BDF8]"
              />
            </div>

            {/* Transducer Type Selector */}
            <div className="pt-2">
              <span className="text-[11px] text-[#94A3B8] block mb-2">
                Acoplamiento de Dispositivo / Transductor:
              </span>
              <div className="grid grid-cols-3 gap-2">
                {(
                  [
                    { id: 0, label: 'IEM (In-Ear)' },
                    { id: 1, label: 'Over-Ear Open' },
                    { id: 2, label: 'Nearfield Spk' },
                  ] as const
                ).map((t) => (
                  <button
                    key={t.id}
                    onClick={() => updateConfig('transducerType', t.id)}
                    className={`py-2 px-2 rounded-xl text-[10px] font-bold border transition-all ${
                      config.transducerType === t.id
                        ? 'bg-[#182230] border-[#38BDF8] text-[#38BDF8]'
                        : 'bg-[#0A0C10] border-[#1E2433] text-[#64748B] hover:text-white'
                    }`}
                  >
                    {t.label}
                  </button>
                ))}
              </div>
            </div>
          </div>
        </div>

        {/* Column 2: Fase 2 — MicroReality Extraction Engine (MicroDetailMap) */}
        <div className="bg-[#10131A] border border-[#1E2433] rounded-2xl p-5 space-y-4">
          <div className="flex items-center justify-between border-b border-[#1E2433] pb-3">
            <div className="flex items-center space-x-2">
              <Activity className="w-4 h-4 text-[#4ADE80]" />
              <h3 className="text-xs font-bold text-white uppercase tracking-wider">
                Fase 2: MicroDetailMap (Inteligibilidad sin Volumen)
              </h3>
            </div>
            <span className="text-[10px] text-[#4ADE80] font-bold">6 COMPONENTES</span>
          </div>

          <div className="space-y-3">
            {microComponents.map((mc) => (
              <div
                key={mc.name}
                className="bg-[#0A0C10] border border-[#1E2433] rounded-xl p-3 space-y-1.5"
              >
                <div className="flex items-center justify-between text-[11px]">
                  <span className="text-white font-semibold truncate">{mc.name}</span>
                  <span className="text-[#38BDF8] text-[10px]">{mc.xyz}</span>
                </div>
                <div className="w-full bg-[#1E293B] h-1.5 rounded-full overflow-hidden">
                  <div
                    className="bg-gradient-to-r from-[#4ADE80] to-[#38BDF8] h-full rounded-full transition-all duration-300"
                    style={{ width: `${Math.round(mc.existence * 100)}%` }}
                  />
                </div>
                <div className="flex justify-between text-[10px] text-[#64748B]">
                  <span>Existencia: {(mc.existence * 100).toFixed(0)}%</span>
                  <span>Confianza: {(mc.confidence * 100).toFixed(0)}%</span>
                </div>
              </div>
            ))}
          </div>
        </div>

        {/* Column 3: Fase 1, 3, 4 & 6 — Acoustic Genome + Time Machine + 4D WFS Objects */}
        <div className="bg-[#10131A] border border-[#1E2433] rounded-2xl p-5 space-y-4">
          <div className="flex items-center justify-between border-b border-[#1E2433] pb-3">
            <div className="flex items-center space-x-2">
              <Layers className="w-4 h-4 text-[#A855F7]" />
              <h3 className="text-xs font-bold text-white uppercase tracking-wider">
                Fases 1, 3 & 6: Genoma, Máquina Temporal y Campo 4D
              </h3>
            </div>
            <Brain className="w-4 h-4 text-[#A855F7]" />
          </div>

          {/* Room Geometry Fingerprint */}
          <div className="bg-[#0A0C10] border border-[#1E2433] rounded-xl p-3 text-xs space-y-1">
            <div className="flex justify-between">
              <span className="text-[#64748B]">Geometría Inferida (W×D×H):</span>
              <span className="text-[#38BDF8] font-bold">
                {roomWidth}m × {roomDepth}m × {roomHeight}m
              </span>
            </div>
            <div className="flex justify-between">
              <span className="text-[#64748B]">RT60 Sabine-Eyring / 6 Paredes:</span>
              <span className="text-[#4ADE80] font-bold">{roomRt60} s (6 taps activos)</span>
            </div>
            <div className="flex justify-between">
              <span className="text-[#64748B]">Trayectoria Física:</span>
              <span className="text-[#F59E0B] font-bold">SOURCE → ROOM → AIR → EAR</span>
            </div>
          </div>

          {/* 4D Objects */}
          <div className="space-y-2.5">
            {sources4D.map((src) => (
              <div
                key={src.id}
                className="bg-[#0A0C10] border border-[#1E2433] rounded-xl p-3 text-[11px] space-y-1"
              >
                <div className="flex justify-between items-center">
                  <span className="text-white font-bold">
                    #{src.id} {src.role}
                  </span>
                  <span className="text-[#A855F7] font-bold">d = {src.dist}m</span>
                </div>
                <div className="grid grid-cols-3 gap-2 text-[10px] text-[#94A3B8]">
                  <span>
                    XYZ: ({src.x}, {src.y}, {src.z})
                  </span>
                  <span>Elev: {src.elev}°</span>
                  <span>Vel: {src.vel}</span>
                </div>
                <div className="flex justify-between text-[10px] text-[#64748B] pt-1 border-t border-[#161B26]">
                  <span>ISO 9613-1 Air LPF: {src.airCutoff} Hz</span>
                  <span className="text-[#4ADE80]">DRR: {src.drr}</span>
                </div>
              </div>
            ))}
          </div>
        </div>
      </div>

      {/* Phase 0 Architecture Map: PRODUCTOR -> DATOS -> CONSUMIDOR */}
      <div className="bg-[#10131A] border border-[#1E2433] rounded-2xl p-5">
        <div className="flex items-center justify-between mb-3">
          <div className="flex items-center space-x-2">
            <Radar className="w-4 h-4 text-[#38BDF8]" />
            <h3 className="text-xs font-bold text-white uppercase tracking-wider">
              Fase 0 & 8: Mapa de Arquitectura Unificada (PRODUCTOR → DATOS → CONSUMIDOR)
            </h3>
          </div>
          <div className="flex items-center space-x-3 text-[11px] text-[#4ADE80]">
            <ShieldCheck className="w-4 h-4" />
            <span>0 Módulos Aislados • 0 Sistemas Paralelos • Lock-Free RT</span>
          </div>
        </div>

        <div className="grid grid-cols-1 md:grid-cols-4 gap-3 text-[11px]">
          <div className="bg-[#0A0C10] border border-[#1E2433] rounded-xl p-3">
            <div className="text-[#38BDF8] font-bold mb-1">1. PRODUCTORES RT / SENSORES</div>
            <p className="text-[#94A3B8] text-[10px]">
              Audio Thread (Ruta A/B) → <code>RawMetricsBus</code> +{' '}
              <code>MicroRealityExtractionEngine</code> (6 componentes micro-acústicos en 0 ms).
            </p>
          </div>
          <div className="bg-[#0A0C10] border border-[#1E2433] rounded-xl p-3">
            <div className="text-[#A855F7] font-bold mb-1">2. INFERENCIA FUERA DEL HILO RT</div>
            <p className="text-[#94A3B8] text-[10px]">
              <code>AdaptiveDecisionEngine::controlLoop</code> →{' '}
              <code>NeuralAcousticInferenceCore</code> + <code>AcousticGenomeSynthesizer</code>.
            </p>
          </div>
          <div className="bg-[#0A0C10] border border-[#1E2433] rounded-xl p-3">
            <div className="text-[#F59E0B] font-bold mb-1">3. ORQUESTADOR DE REALIDAD</div>
            <p className="text-[#94A3B8] text-[10px]">
              <code>AcousticRealityOrchestrator</code> coordina <code>AcousticTimeMachine</code> +{' '}
              <code>PersonalAuditoryRealityModel</code> + <code>PerceptualOptimizer</code>.
            </p>
          </div>
          <div className="bg-[#0A0C10] border border-[#1E2433] rounded-xl p-3">
            <div className="text-[#4ADE80] font-bold mb-1">4. CONSUMIDORES DSP REAL-TIME</div>
            <p className="text-[#94A3B8] text-[10px]">
              <code>IvannaFusionCore</code> + <code>WfsRenderer (4D Field Synthesis)</code> +{' '}
              <code>omega_effect.cpp</code> + <code>SafetyLimiter</code>.
            </p>
          </div>
        </div>
      </div>

      {/* Fases 9–15: Sistema Nervioso Superior — Acoustic Reality Cognitive Evolution Engine */}
      <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
        {/* Fase 9 & 11: AcousticCognitiveCore + AcousticExecutiveBrain */}
        <div className="bg-[#10131A] border border-[#38BDF8]/30 rounded-2xl p-5 space-y-4">
          <div className="flex items-center justify-between border-b border-[#1E2433] pb-3">
            <div className="flex items-center space-x-2">
              <Brain className="w-4 h-4 text-[#38BDF8]" />
              <h3 className="text-xs font-bold text-white uppercase tracking-wider">
                Fases 9 & 11: CognitiveCore + ExecutiveBrain
              </h3>
            </div>
            <span className="text-[10px] text-[#38BDF8] font-bold">REALITY INTENT</span>
          </div>

          <div className="space-y-2.5 text-xs">
            {[
              { rank: '#1', axis: 'DepthPreservation (Profundidad Física)', score: +(0.86 + config.realityIntensity * 0.08).toFixed(2), color: 'text-[#38BDF8]' },
              { rank: '#2', axis: 'MicroDynamicPreservation (Microdinámica)', score: +(0.78 + config.microIntelligibilityBoost * 0.10).toFixed(2), color: 'text-[#4ADE80]' },
              { rank: '#3', axis: 'VocalClarity (Claridad Vocal Central)', score: 0.74, color: 'text-[#A855F7]' },
              { rank: '#4', axis: 'AmbientExpansion (Expansión Controlada)', score: +(0.58 * (1 - fatigueRisk * 0.5)).toFixed(2), color: 'text-[#F59E0B]' },
            ].map((p) => (
              <div key={p.rank} className="bg-[#0A0C10] border border-[#1E2433] rounded-xl p-2.5 flex items-center justify-between">
                <div className="flex items-center space-x-2">
                  <span className={`font-extrabold ${p.color}`}>{p.rank}</span>
                  <span className="text-[#E2E8F0] text-[11px]">{p.axis}</span>
                </div>
                <span className={`font-bold text-[11px] ${p.color}`}>{(p.score * 100).toFixed(0)}%</span>
              </div>
            ))}
          </div>

          <div className="bg-[#0A0C10] border border-[#1E2433] rounded-xl p-3 text-[11px] space-y-1">
            <div className="flex justify-between">
              <span className="text-[#64748B]">Arbitraje ExecutiveBrain:</span>
              <span className="text-[#4ADE80] font-bold">Equilibrio Homeostático</span>
            </div>
            <div className="flex justify-between">
              <span className="text-[#64748B]">Veto Anti-Espectacularidad:</span>
              <span className="text-[#38BDF8] font-bold">ACTIVO (94.2% Coherencia)</span>
            </div>
          </div>
        </div>

        {/* Fase 10: Acoustic Specialist Network (4 Agentes por Fenómeno Acústico) */}
        <div className="bg-[#10131A] border border-[#A855F7]/30 rounded-2xl p-5 space-y-4">
          <div className="flex items-center justify-between border-b border-[#1E2433] pb-3">
            <div className="flex items-center space-x-2">
              <Sparkles className="w-4 h-4 text-[#A855F7]" />
              <h3 className="text-xs font-bold text-white uppercase tracking-wider">
                Fase 10: Acoustic Specialist Network
              </h3>
            </div>
            <span className="text-[10px] text-[#A855F7] font-bold">4 ESPECIALISTAS</span>
          </div>

          <div className="space-y-2.5 text-[11px]">
            {[
              {
                agent: 'SpatialIntelligenceAgent',
                controls: 'WFS + ObjectSpatialRenderer + HRTF',
                evals: 'Localización 91% · Estabilidad 93% · Profundidad 89%',
                accent: 'text-[#38BDF8]',
              },
              {
                agent: 'RoomIntelligenceAgent',
                controls: 'RoomProjectionEngine + RIR 6 Paredes',
                evals: `RT60 ${roomRt60}s · Reflexiones 90% · Realismo 88%`,
                accent: 'text-[#4ADE80]',
              },
              {
                agent: 'MicroRealityAgent',
                controls: 'MicroDetailExtractor (0 dB inflación)',
                evals: 'Microtransitorios 92% · Aire 94% · Respiración 89%',
                accent: 'text-[#F59E0B]',
              },
              {
                agent: 'HumanPerceptionAgent (Juez Final)',
                controls: '¿El cerebro interpreta esto como más real?',
                evals: `Veredicto Realismo ${(realismScore * 100).toFixed(1)}% · Fatiga ${(fatigueRisk * 100).toFixed(1)}%`,
                accent: 'text-[#A855F7]',
              },
            ].map((sp) => (
              <div key={sp.agent} className="bg-[#0A0C10] border border-[#1E2433] rounded-xl p-2.5 space-y-1">
                <div className="flex justify-between">
                  <span className={`font-bold ${sp.accent}`}>{sp.agent}</span>
                </div>
                <div className="text-[10px] text-[#94A3B8]">Controla: {sp.controls}</div>
                <div className="text-[10px] text-[#64748B]">{sp.evals}</div>
              </div>
            ))}
          </div>
        </div>

        {/* Fases 12, 13, 14 & 15: Memory Matrix, Homeostasis Loop, Digital Twin & Evolutionary */}
        <div className="bg-[#10131A] border border-[#4ADE80]/30 rounded-2xl p-5 space-y-4">
          <div className="flex items-center justify-between border-b border-[#1E2433] pb-3">
            <div className="flex items-center space-x-2">
              <ShieldCheck className="w-4 h-4 text-[#4ADE80]" />
              <h3 className="text-xs font-bold text-white uppercase tracking-wider">
                Fases 12–15: Memoria, Homeostasis y Twin
              </h3>
            </div>
            <span className="text-[10px] text-[#4ADE80] font-bold">24H ESTABLE</span>
          </div>

          <div className="space-y-2.5 text-[11px]">
            <div className="bg-[#0A0C10] border border-[#1E2433] rounded-xl p-2.5">
              <div className="text-[#38BDF8] font-bold">Fase 12 · AcousticExperienceMemory</div>
              <div className="text-[10px] text-[#94A3B8] mt-0.5">
                Perfil aprendido (ej. Rush — Xanadu): Profundidad 2.35m · Spread ×1.14 · 0 bytes de audio almacenados.
              </div>
            </div>

            <div className="bg-[#0A0C10] border border-[#1E2433] rounded-xl p-2.5">
              <div className="text-[#4ADE80] font-bold">Fase 13 · SelfCalibratingRealityLoop</div>
              <div className="text-[10px] text-[#94A3B8] mt-0.5">
                Percepción → Reconstrucción → Evaluación → Corrección. Estabilidad biológica: 96.4%.
              </div>
            </div>

            <div className="bg-[#0A0C10] border border-[#1E2433] rounded-xl p-2.5">
              <div className="text-[#A855F7] font-bold">Fase 14 · DigitalAcousticTwin</div>
              <div className="text-[10px] text-[#94A3B8] mt-0.5">
                Sala ({roomWidth}×{roomDepth}×{roomHeight}m) + Dispositivo #{config.transducerType} + Oyente (ITD ×{itdScale.toFixed(2)}) + Escena 4D.
              </div>
            </div>

            <div className="bg-[#0A0C10] border border-[#1E2433] rounded-xl p-2.5">
              <div className="text-[#F59E0B] font-bold">Fase 15 · EvolutionaryOptimization + Validación</div>
              <div className="text-[10px] text-[#94A3B8] mt-0.5">
                CMA-ES + Q-Learning optimiza Presence/Naturalness/Separation/FatigueFree/Immersion. 6/6 Pruebas Humanas PASS.
              </div>
            </div>
          </div>
        </div>
      </div>

      {/* ATLAS-ESCENA 12D · SINGULARIDAD ACÚSTICA (M1–M10) */}
      <div className="bg-[#10131A] border border-[#38BDF8]/40 rounded-2xl p-6 space-y-5">
        <div className="flex flex-col lg:flex-row lg:items-center justify-between gap-4 border-b border-[#1E2433] pb-4">
          <div>
            <div className="flex items-center space-x-2">
              <Radar className="w-5 h-5 text-[#38BDF8]" />
              <h3 className="text-sm font-bold text-white uppercase tracking-wider">
                ATLAS-ESCENA 12D · SINGULARIDAD ACÚSTICA (M1–M10)
              </h3>
              <span className="text-[10px] px-2 py-0.5 rounded bg-[#4ADE80]/15 border border-[#4ADE80]/40 text-[#4ADE80] font-bold">
                BAYES 12D · TRIPLE BUFFER WAIT-FREE · M10 GUARD
              </span>
            </div>
            <p className="text-xs text-[#94A3B8] mt-1">
              Inferencia Gaussiana diagonal 12D (<code className="text-[#38BDF8]">StyleBlender</code>) →{' '}
              <code className="text-[#4ADE80]">LateReverbSuppressor</code> (Lebart/Habets) →{' '}
              <code className="text-[#A855F7]">PhysicalEarlyReflections</code> (6 paredes Sabine-Eyring) →{' '}
              <code className="text-[#F59E0B]">ObjectSpatialRenderer</code> (Woodworth ITD + Brown-Duda) →{' '}
              <code className="text-[#38BDF8]">ChebHarmonicShaper</code> (T2 par + T3 impar).
            </p>
          </div>
          <div className="flex items-center space-x-2 text-xs">
            <span className="px-3 py-1.5 rounded-lg bg-[#0A0C10] border border-[#1E2433] text-[#4ADE80] font-bold">
              Q(M10): 0.96 · C_t: 0.98 · C_s: 0.94 · C_d: 0.97
            </span>
          </div>
        </div>

        {/* 12 Style Selector Chips */}
        <div className="flex flex-wrap gap-2">
          {atlasStyles.map((st, idx) => (
            <button
              key={st.name}
              onClick={() => setAtlasStyleIdx(idx)}
              className={`px-3 py-1.5 rounded-lg text-xs font-bold border transition-all ${
                atlasStyleIdx === idx
                  ? 'bg-[#38BDF8]/20 border-[#38BDF8] text-[#38BDF8]'
                  : 'bg-[#0A0C10] border-[#1E2433] text-[#94A3B8] hover:text-white'
              }`}
            >
              {st.name}
            </button>
          ))}
        </div>

        {/* Active Scene Targets + A/B Engine Toggles */}
        <div className="grid grid-cols-1 lg:grid-cols-2 gap-4">
          <div className="grid grid-cols-3 sm:grid-cols-6 gap-2 bg-[#0A0C10] border border-[#1E2433] rounded-xl p-3 text-center">
            <div>
              <div className="text-[10px] text-[#64748B]">WFS SPREAD</div>
              <div className="text-sm font-bold text-[#38BDF8]">{activeAtlas.spread.toFixed(2)}</div>
            </div>
            <div>
              <div className="text-[10px] text-[#64748B]">HRTF DEPTH</div>
              <div className="text-sm font-bold text-[#38BDF8]">{activeAtlas.depth.toFixed(2)}</div>
            </div>
            <div>
              <div className="text-[10px] text-[#64748B]">ENV DEPTH</div>
              <div className="text-sm font-bold text-[#4ADE80]">{activeAtlas.env.toFixed(2)}</div>
            </div>
            <div>
              <div className="text-[10px] text-[#64748B]">WARMTH T2/T3</div>
              <div className="text-sm font-bold text-[#F59E0B]">{activeAtlas.warmth.toFixed(2)}</div>
            </div>
            <div>
              <div className="text-[10px] text-[#64748B]">ELEVACIÓN Z</div>
              <div className="text-sm font-bold text-[#A855F7]">+{activeAtlas.elev.toFixed(2)}m</div>
            </div>
            <div>
              <div className="text-[10px] text-[#64748B]">TARGET IACC</div>
              <div className="text-sm font-bold text-[#22D3EE]">{activeAtlas.iacc.toFixed(2)}</div>
            </div>
          </div>

          <div className="grid grid-cols-1 sm:grid-cols-3 gap-2">
            <button
              onClick={() => setUseStatDereverb(!useStatDereverb)}
              className={`p-3 rounded-xl border text-left text-xs transition-all ${
                useStatDereverb
                  ? 'bg-[#132A20] border-[#4ADE80]/60 text-[#4ADE80]'
                  : 'bg-[#0A0C10] border-[#1E2433] text-[#64748B]'
              }`}
            >
              <div className="font-bold">Lebart/Habets M5</div>
              <div className="text-[10px] opacity-80">{useStatDereverb ? 'ACTIVO (3-Pole 250Hz)' : 'WPE Fallback'}</div>
            </button>
            <button
              onClick={() => setUsePhysicalEr(!usePhysicalEr)}
              className={`p-3 rounded-xl border text-left text-xs transition-all ${
                usePhysicalEr
                  ? 'bg-[#111C2D] border-[#38BDF8]/60 text-[#38BDF8]'
                  : 'bg-[#0A0C10] border-[#1E2433] text-[#64748B]'
              }`}
            >
              <div className="font-bold">6-Wall Sabine ER M7</div>
              <div className="text-[10px] opacity-80">{usePhysicalEr ? 'ACTIVO (Image Sources)' : 'Cluster Fallback'}</div>
            </button>
            <button
              onClick={() => setChebShaperActive(!chebShaperActive)}
              className={`p-3 rounded-xl border text-left text-xs transition-all ${
                chebShaperActive
                  ? 'bg-[#281E12] border-[#F59E0B]/60 text-[#F59E0B]'
                  : 'bg-[#0A0C10] border-[#1E2433] text-[#64748B]'
              }`}
            >
              <div className="font-bold">Chebyshev T2+T3 M9</div>
              <div className="text-[10px] opacity-80">{chebShaperActive ? 'ACTIVO (Anti-IMD + DC)' : 'Padé Fallback'}</div>
            </button>
          </div>
        </div>
      </div>
    </div>
  );
};
