import React from 'react';
import { DspParameters, BenchmarkMetrics } from '../types';
import { MagiskIntegrationPanel } from './MagiskIntegrationPanel';
import { Sliders, ShieldAlert, Cpu, Waves, Sparkles, RotateCcw, Activity, Zap, CheckCircle2, Terminal, RefreshCw, Layers } from 'lucide-react';

interface MasterControlPlaneProps {
  params: DspParameters;
  metrics: BenchmarkMetrics;
  onParamChange: (key: keyof DspParameters, value: any) => void;
  onResetClipCount: () => void;
  onRunAutoCalibration: () => void;
  onApplyPreset: (preset: DspParameters['activePreset']) => void;
}

export const MasterControlPlane: React.FC<MasterControlPlaneProps> = ({
  params,
  metrics,
  onParamChange,
  onResetClipCount,
  onRunAutoCalibration,
  onApplyPreset,
}) => {
  return (
    <div className="space-y-6 font-mono text-xs">
      
      {/* Magisk IPC Panel */}
      <MagiskIntegrationPanel />
      
      {/* Header Banner & Auto-Calibration CTA */}
      <div className="bg-[#10131A] border border-[#232936] rounded-xl p-5 shadow-lg relative overflow-hidden">
        <div className="flex flex-col lg:flex-row lg:items-center justify-between gap-4 relative z-10">
          <div>
            <div className="flex items-center gap-2">
              <span className="text-[10px] px-2 py-0.5 rounded bg-[#182230] text-[#38BDF8] border border-[#243346] font-bold">
                JNI C++ HARDWARE KERNEL
              </span>
              {metrics.lastCalibratedAt && (
                <span className="text-[10px] text-[#4ADE80] font-semibold flex items-center gap-1">
                  <CheckCircle2 className="w-3 h-3 text-[#4ADE80]" />
                  SIMD Calibrated {metrics.lastCalibratedAt}
                </span>
              )}
            </div>
            <h2 className="text-sm font-bold text-white flex items-center gap-2 uppercase tracking-wide mt-1">
              <Sliders className="w-4 h-4 text-[#38BDF8]" />
              Master Control Plane & SIMD Buffer Optimizer
            </h2>
            <p className="text-xs text-[#64748B] mt-1 max-w-3xl">
              Lock-free kernel controls linked to <code className="text-[#38BDF8]">IvannaNativeLib</code>. Auto-calibrate SIMD buffers to optimize ARM NEON vector unroll and reduce audio thread jitter.
            </p>
          </div>

          <div className="flex flex-wrap items-center gap-3">
            {/* Auto-Calibration Button */}
            <button
              onClick={onRunAutoCalibration}
              disabled={metrics.isCalibrating}
              className={`flex items-center gap-2 px-4 py-2.5 rounded-lg font-bold text-xs shadow-md transition-all border ${
                metrics.isCalibrating
                  ? 'bg-[#18261E] border-[#4ADE80] text-[#4ADE80] animate-pulse cursor-not-allowed'
                  : 'bg-gradient-to-r from-[#38BDF8] to-[#0284C7] hover:from-[#0284C7] hover:to-[#0369A1] text-[#0A0C10] border-[#38BDF8] shadow-[#38BDF8]/20 hover:scale-[1.02]'
              }`}
            >
              {metrics.isCalibrating ? (
                <>
                  <RefreshCw className="w-4 h-4 animate-spin text-[#4ADE80]" />
                  <span>Measuring Kernel Throughput ({metrics.calibrationProgress}%)...</span>
                </>
              ) : (
                <>
                  <Zap className="w-4 h-4 fill-current" />
                  <span>Auto-Calibración SIMD</span>
                </>
              )}
            </button>

            {/* Meters & Latency display */}
            <div className="flex items-center gap-2">
              <div className="px-3 py-1.5 bg-[#0A0C10] border border-[#1A1D24] rounded-lg flex items-center gap-2">
                <div>
                  <span className="text-[#64748B] block text-[9px] font-bold uppercase">CLIPS</span>
                  <span className={`text-xs font-bold ${metrics.clipCount > 0 ? 'text-[#FF6188]' : 'text-[#4ADE80]'}`}>
                    {metrics.clipCount}
                  </span>
                </div>
                <button
                  onClick={onResetClipCount}
                  className="p-1 bg-[#1E2330] hover:bg-[#2A3142] text-[#94A3B8] hover:text-white rounded transition-colors"
                  title="Reset Clip Meter"
                >
                  <RotateCcw className="w-3 h-3" />
                </button>
              </div>

              <div className="px-3 py-1.5 bg-[#0A0C10] border border-[#1A1D24] rounded-lg">
                <span className="text-[#64748B] block text-[9px] font-bold uppercase">LATENCY</span>
                <span className="text-[#38BDF8] font-bold text-xs">{metrics.blockLatencyMicroseconds.toFixed(1)} µs</span>
              </div>
            </div>
          </div>
        </div>

        {/* Calibration Progress Bar */}
        {metrics.isCalibrating && (
          <div className="mt-4 pt-3 border-t border-[#1E2330] space-y-2">
            <div className="flex justify-between items-center text-[11px]">
              <span className="text-[#38BDF8] font-bold flex items-center gap-2">
                <Terminal className="w-3.5 h-3.5 animate-pulse" />
                Profiling ARM NEON Vector Registers & SPSC Ring Buffer...
              </span>
              <span className="text-[#4ADE80] font-bold">{metrics.calibrationProgress}%</span>
            </div>
            <div className="w-full bg-[#1A1D24] rounded-full h-2 overflow-hidden border border-[#232936]">
              <div
                className="bg-gradient-to-r from-[#38BDF8] via-[#4ADE80] to-[#F59E0B] h-full transition-all duration-300"
                style={{ width: `${metrics.calibrationProgress}%` }}
              />
            </div>
            {metrics.calibrationLog.length > 0 && (
              <div className="bg-[#0A0C10] border border-[#1E2330] p-2.5 rounded font-mono text-[10px] text-[#94A3B8] max-h-24 overflow-y-auto space-y-1">
                {metrics.calibrationLog.map((log, idx) => (
                  <div key={idx} className="flex items-center gap-2">
                    <span className="text-[#38BDF8]">&gt;</span>
                    <span>{log}</span>
                  </div>
                ))}
              </div>
            )}
          </div>
        )}
      </div>

      {/* Evolutive Presets Selector Section */}
      <div className="bg-[#10131A] border border-[#232936] rounded-xl p-4 space-y-3">
        <div className="flex items-center justify-between border-b border-[#1E2330] pb-2">
          <div className="flex items-center gap-2">
            <Layers className="w-4 h-4 text-[#F59E0B]" />
            <h3 className="font-bold text-white uppercase text-xs">Presets Evolutivos & Adaptive Profiles</h3>
          </div>
          <span className="text-[10px] text-[#64748B]">Active Profile: <strong className="text-white uppercase">{params.activePreset.replace(/_/g, ' ')}</strong></span>
        </div>

        <div className="grid grid-cols-2 sm:grid-cols-3 md:grid-cols-6 gap-2">
          {[
            { id: 'anti_dolby_extreme', name: 'Anti-Dolby Max', desc: '100% Phase Neutralization', color: 'hover:border-[#38BDF8] text-[#38BDF8]' },
            { id: 'audiophile', name: 'Audiophile 3D', desc: '3D HRTF + Smooth Alpha', color: 'hover:border-[#A855F7] text-[#A855F7]' },
            { id: 'bass_head', name: 'Bass Harmonic', desc: 'Chebyshev 2x Drive Boost', color: 'hover:border-[#F97316] text-[#F97316]' },
            { id: 'vocal_protect', name: 'Vocal Protect', desc: 'Fatigue Suppression & Comp', color: 'hover:border-[#4ADE80] text-[#4ADE80]' },
            { id: 'gaming_spatial', name: 'Gaming Spatial', desc: 'Ultra Low-Latency Spatial', color: 'hover:border-[#F59E0B] text-[#F59E0B]' },
            { id: 'evo_cma_es', name: 'Evolutive CMA', desc: '512-Band Genetic EQ', color: 'hover:border-[#EC4899] text-[#EC4899]' },
          ].map((preset) => (
            <button
              key={preset.id}
              onClick={() => onApplyPreset(preset.id as DspParameters['activePreset'])}
              className={`p-2.5 rounded-lg border text-left transition-all ${
                params.activePreset === preset.id
                  ? 'bg-[#1E2330] border-[#38BDF8] shadow-md'
                  : 'bg-[#0A0C10] border-[#1E2330] text-[#64748B] hover:bg-[#141822]'
              } ${preset.color}`}
            >
              <div className="font-bold text-[11px] text-white truncate">{preset.name}</div>
              <div className="text-[9px] text-[#64748B] truncate mt-0.5">{preset.desc}</div>
            </button>
          ))}
        </div>
      </div>

      {/* Grid of Controls: Buffer, Anti-Dolby, NHO, Spatial, Golden Ear, Comp */}
      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-5">
        
        {/* 0. Kernel Buffer & SIMD Efficiency Control */}
        <div className="bg-[#10131A] border border-[#232936] rounded-xl p-5 space-y-4 hover:border-[#38BDF8]/50 transition-colors">
          <div className="flex items-center justify-between border-b border-[#1E2330] pb-2.5">
            <div className="flex items-center gap-2">
              <Cpu className="w-4 h-4 text-[#38BDF8]" />
              <h3 className="font-bold text-white uppercase text-xs">Kernel Buffer & SIMD Vector</h3>
            </div>
            <span className="text-[10px] px-2 py-0.5 bg-[#182230] text-[#38BDF8] border border-[#243346] rounded font-bold">
              ARM NEON
            </span>
          </div>

          {/* Block Size Selector */}
          <div className="space-y-2">
            <div className="flex justify-between items-center text-xs">
              <span className="text-[#94A3B8] font-semibold">Processing Block Size:</span>
              <span className="text-[#38BDF8] font-bold">{params.blockSize} samples</span>
            </div>
            <div className="grid grid-cols-5 gap-1.5">
              {[128, 256, 512, 1024, 2048].map((size) => (
                <button
                  key={size}
                  onClick={() => onParamChange('blockSize', size)}
                  className={`py-1 rounded text-[10px] font-bold border transition-colors ${
                    params.blockSize === size
                      ? 'bg-[#38BDF8] text-[#0A0C10] border-[#38BDF8]'
                      : 'bg-[#0A0C10] border-[#1E2330] text-[#64748B] hover:text-white'
                  }`}
                >
                  {size}
                </button>
              ))}
            </div>
          </div>

          {/* Sample Rate Selector */}
          <div className="space-y-2 pt-2 border-t border-[#1E2330]">
            <div className="flex justify-between items-center text-xs">
              <span className="text-[#94A3B8] font-semibold">Sample Rate:</span>
              <span className="text-white font-bold">{(params.sampleRate / 1000).toFixed(1)} kHz</span>
            </div>
            <div className="grid grid-cols-4 gap-1.5">
              {[44100, 48000, 96000, 192000].map((rate) => (
                <button
                  key={rate}
                  onClick={() => onParamChange('sampleRate', rate)}
                  className={`py-1 rounded text-[10px] font-bold border transition-colors ${
                    params.sampleRate === rate
                      ? 'bg-white text-[#0A0C10] border-white'
                      : 'bg-[#0A0C10] border-[#1E2330] text-[#64748B] hover:text-white'
                  }`}
                >
                  {rate / 1000}k
                </button>
              ))}
            </div>
          </div>

          {/* SIMD Telemetry Badge */}
          <div className="p-2.5 bg-[#0A0C10] rounded border border-[#1E2330] space-y-1 text-[10px]">
            <div className="flex justify-between">
              <span className="text-[#64748B]">SIMD Vector Efficiency:</span>
              <span className="text-[#4ADE80] font-bold">{metrics.simdEfficiencyPercent}%</span>
            </div>
            <div className="flex justify-between">
              <span className="text-[#64748B]">GFLOPS Throughput:</span>
              <span className="text-[#38BDF8] font-bold">{metrics.gflopsThroughput.toFixed(1)} GFLOPS</span>
            </div>
            <div className="flex justify-between">
              <span className="text-[#64748B]">Active Vector Registers:</span>
              <span className="text-[#A855F7] font-bold">{metrics.registersActiveCount} Q-Registers</span>
            </div>
          </div>
        </div>

        {/* 1. Anti-Dolby & Master Output Control */}
        <div className="bg-[#10131A] border border-[#232936] rounded-xl p-5 space-y-4 hover:border-[#38BDF8]/50 transition-colors">
          <div className="flex items-center justify-between border-b border-[#1E2330] pb-2.5">
            <div className="flex items-center gap-2">
              <ShieldAlert className="w-4 h-4 text-[#38BDF8]" />
              <h3 className="font-bold text-white uppercase text-xs">Anti-Dolby Engine</h3>
            </div>
            <span className="text-[10px] px-2 py-0.5 bg-[#182230] text-[#38BDF8] border border-[#243346] rounded font-bold">
              JNI Active
            </span>
          </div>

          {/* Anti-Dolby Intensity */}
          <div className="space-y-2">
            <div className="flex justify-between items-center text-xs">
              <span className="text-[#94A3B8] font-semibold">Anti-Dolby Intensity:</span>
              <span className="text-[#38BDF8] font-bold text-sm">{(params.antiDolbyIntensity * 100).toFixed(0)}%</span>
            </div>
            <input
              type="range"
              min="0.0"
              max="1.0"
              step="0.01"
              value={params.antiDolbyIntensity}
              onChange={(e) => onParamChange('antiDolbyIntensity', parseFloat(e.target.value))}
              className="w-full accent-[#38BDF8] bg-[#1A1D24] rounded h-1.5 cursor-pointer"
            />
            <div className="flex justify-between text-[10px] text-[#64748B]">
              <span>0% (Bypass)</span>
              <span>100% (Full Neutralization)</span>
            </div>
          </div>

          {/* Master Output Gain */}
          <div className="space-y-2 pt-2 border-t border-[#1E2330]">
            <div className="flex justify-between items-center text-xs">
              <span className="text-[#94A3B8] font-semibold">Master Gain Output:</span>
              <span className="text-white font-bold">{params.masterGain.toFixed(2)}x</span>
            </div>
            <input
              type="range"
              min="0.0"
              max="2.0"
              step="0.05"
              value={params.masterGain}
              onChange={(e) => onParamChange('masterGain', parseFloat(e.target.value))}
              className="w-full accent-white bg-[#1A1D24] rounded h-1.5 cursor-pointer"
            />
            <div className="flex justify-between text-[10px] text-[#64748B]">
              <span>0.0x (Mute)</span>
              <span>2.0x (+6dB Boost)</span>
            </div>
          </div>
        </div>

        {/* 2. NHO Acoustic Parameters */}
        <div className="bg-[#10131A] border border-[#232936] rounded-xl p-5 space-y-4 hover:border-[#4ADE80]/50 transition-colors">
          <div className="flex items-center justify-between border-b border-[#1E2330] pb-2.5">
            <div className="flex items-center gap-2">
              <Activity className="w-4 h-4 text-[#4ADE80]" />
              <h3 className="font-bold text-white uppercase text-xs">NHO Acoustic Engine</h3>
            </div>
            <span className="text-[10px] px-2 py-0.5 bg-[#18261E] text-[#4ADE80] border border-[#23382B] rounded font-bold">
              ARM SIMD
            </span>
          </div>

          {/* NHO Alpha */}
          <div className="space-y-1.5">
            <div className="flex justify-between items-center text-xs">
              <span className="text-[#94A3B8]">NHO Alpha (Filter Damping):</span>
              <span className="text-[#4ADE80] font-bold">{params.nhoAlpha.toFixed(2)}</span>
            </div>
            <input
              type="range"
              min="0.0"
              max="1.0"
              step="0.02"
              value={params.nhoAlpha}
              onChange={(e) => onParamChange('nhoAlpha', parseFloat(e.target.value))}
              className="w-full accent-[#4ADE80] bg-[#1A1D24] rounded h-1.5 cursor-pointer"
            />
          </div>

          {/* NHO Beta */}
          <div className="space-y-1.5">
            <div className="flex justify-between items-center text-xs">
              <span className="text-[#94A3B8]">NHO Beta (Spectral Slope):</span>
              <span className="text-[#4ADE80] font-bold">{params.nhoBeta.toFixed(2)}</span>
            </div>
            <input
              type="range"
              min="0.0"
              max="1.0"
              step="0.02"
              value={params.nhoBeta}
              onChange={(e) => onParamChange('nhoBeta', parseFloat(e.target.value))}
              className="w-full accent-[#4ADE80] bg-[#1A1D24] rounded h-1.5 cursor-pointer"
            />
          </div>

          {/* NHO Wet Eta */}
          <div className="space-y-1.5">
            <div className="flex justify-between items-center text-xs">
              <span className="text-[#94A3B8]">NHO Wet Eta (Mix Ratio):</span>
              <span className="text-white font-bold">{(params.spatialWetEta * 100).toFixed(0)}%</span>
            </div>
            <input
              type="range"
              min="0.0"
              max="1.0"
              step="0.02"
              value={params.spatialWetEta}
              onChange={(e) => onParamChange('spatialWetEta', parseFloat(e.target.value))}
              className="w-full accent-white bg-[#1A1D24] rounded h-1.5 cursor-pointer"
            />
          </div>
        </div>

        {/* 3. Spatial Soundstage Parameters */}
        <div className="bg-[#10131A] border border-[#232936] rounded-xl p-5 space-y-4 hover:border-[#A855F7]/50 transition-colors">
          <div className="flex items-center justify-between border-b border-[#1E2330] pb-2.5">
            <div className="flex items-center gap-2">
              <Cpu className="w-4 h-4 text-[#A855F7]" />
              <h3 className="font-bold text-white uppercase text-xs">3D Spatial Matrix</h3>
            </div>
            <span className="text-[10px] px-2 py-0.5 bg-[#23182E] text-[#A855F7] border border-[#352246] rounded font-bold">
              Rayleigh Head
            </span>
          </div>

          {/* Spatial Angle */}
          <div className="space-y-1.5">
            <div className="flex justify-between items-center text-xs">
              <span className="text-[#94A3B8]">Spatial Angle Azimuth:</span>
              <span className="text-[#A855F7] font-bold">{params.spatialAngleDeg.toFixed(0)}°</span>
            </div>
            <input
              type="range"
              min="0"
              max="90"
              step="1"
              value={params.spatialAngleDeg}
              onChange={(e) => onParamChange('spatialAngleDeg', parseFloat(e.target.value))}
              className="w-full accent-[#A855F7] bg-[#1A1D24] rounded h-1.5 cursor-pointer"
            />
          </div>

          {/* Direct Width */}
          <div className="space-y-1.5">
            <div className="flex justify-between items-center text-xs">
              <span className="text-[#94A3B8]">Direct Width Expansion:</span>
              <span className="text-[#A855F7] font-bold">{params.spatialWidth.toFixed(2)}x</span>
            </div>
            <input
              type="range"
              min="0.0"
              max="2.0"
              step="0.05"
              value={params.spatialWidth}
              onChange={(e) => onParamChange('spatialWidth', parseFloat(e.target.value))}
              className="w-full accent-[#A855F7] bg-[#1A1D24] rounded h-1.5 cursor-pointer"
            />
          </div>

          {/* HRTF Toggle */}
          <div className="flex items-center justify-between pt-2 border-t border-[#1E2330]">
            <span className="text-[#CBD5E1] font-semibold">3D HRTF Convolution:</span>
            <button
              onClick={() => onParamChange('hrtfEnabled', !params.hrtfEnabled)}
              className={`px-3 py-1 rounded text-[10px] font-bold transition-all border ${
                params.hrtfEnabled
                  ? 'bg-[#23182E] border-[#A855F7] text-[#A855F7]'
                  : 'bg-[#12151C] border-[#1E2330] text-[#64748B]'
              }`}
            >
              {params.hrtfEnabled ? 'ENABLED' : 'DISABLED'}
            </button>
          </div>
        </div>

        {/* 4. Golden Ear & Harmonic Gain */}
        <div className="bg-[#10131A] border border-[#232936] rounded-xl p-5 space-y-4 hover:border-[#F97316]/50 transition-colors">
          <div className="flex items-center justify-between border-b border-[#1E2330] pb-2.5">
            <div className="flex items-center gap-2">
              <Sparkles className="w-4 h-4 text-[#F97316]" />
              <h3 className="font-bold text-white uppercase text-xs">Golden Ear Exciter</h3>
            </div>
            <button
              onClick={() => onParamChange('goldenEarEnabled', !params.goldenEarEnabled)}
              className={`px-2 py-0.5 rounded text-[10px] font-bold transition-all border ${
                params.goldenEarEnabled
                  ? 'bg-[#2D1B14] border-[#F97316] text-[#F97316]'
                  : 'bg-[#12151C] border-[#1E2330] text-[#64748B]'
              }`}
            >
              {params.goldenEarEnabled ? 'GAN ON' : 'GAN OFF'}
            </button>
          </div>

          {/* Golden Ear Drive */}
          <div className="space-y-1.5">
            <div className="flex justify-between items-center text-xs">
              <span className="text-[#94A3B8]">Harmonic Chebyshev Drive:</span>
              <span className="text-[#F97316] font-bold">{params.goldenEarDrive.toFixed(2)}x</span>
            </div>
            <input
              type="range"
              min="1.0"
              max="3.0"
              step="0.05"
              value={params.goldenEarDrive}
              onChange={(e) => onParamChange('goldenEarDrive', parseFloat(e.target.value))}
              className="w-full accent-[#F97316] bg-[#1A1D24] rounded h-1.5 cursor-pointer"
            />
          </div>

          {/* Harmonic Gain Boost */}
          <div className="space-y-1.5">
            <div className="flex justify-between items-center text-xs">
              <span className="text-[#94A3B8]">Harmonic Gain Boost:</span>
              <span className="text-[#F97316] font-bold">{params.harmonicGain.toFixed(2)}x</span>
            </div>
            <input
              type="range"
              min="0.0"
              max="2.0"
              step="0.05"
              value={params.harmonicGain}
              onChange={(e) => onParamChange('harmonicGain', parseFloat(e.target.value))}
              className="w-full accent-[#F97316] bg-[#1A1D24] rounded h-1.5 cursor-pointer"
            />
          </div>
        </div>

        {/* 5. Dynamic Compressor & Limiter */}
        <div className="bg-[#10131A] border border-[#232936] rounded-xl p-5 space-y-4 hover:border-[#38BDF8]/50 transition-colors md:col-span-2 lg:col-span-2">
          <div className="flex items-center justify-between border-b border-[#1E2330] pb-2.5">
            <div className="flex items-center gap-2">
              <Waves className="w-4 h-4 text-[#38BDF8]" />
              <h3 className="font-bold text-white uppercase text-xs">Dynamic Range Compressor / Limiter</h3>
            </div>
            <button
              onClick={() => onParamChange('adaptEnabled', !params.adaptEnabled)}
              className={`px-2.5 py-0.5 rounded text-[10px] font-bold transition-all border ${
                params.adaptEnabled
                  ? 'bg-[#182230] border-[#38BDF8] text-[#38BDF8]'
                  : 'bg-[#12151C] border-[#1E2330] text-[#64748B]'
              }`}
            >
              {params.adaptEnabled ? 'ADAPTIVE COMP ON' : 'ADAPTIVE COMP OFF'}
            </button>
          </div>

          <div className="grid grid-cols-1 sm:grid-cols-2 md:grid-cols-4 gap-4">
            <div className="space-y-1.5">
              <div className="flex justify-between items-center text-[11px]">
                <span className="text-[#94A3B8]">Threshold:</span>
                <span className="text-[#38BDF8] font-bold">{params.compThresholdDb.toFixed(1)} dB</span>
              </div>
              <input
                type="range"
                min="-60"
                max="0"
                step="1"
                value={params.compThresholdDb}
                onChange={(e) => onParamChange('compThresholdDb', parseFloat(e.target.value))}
                className="w-full accent-[#38BDF8] bg-[#1A1D24] rounded h-1.5 cursor-pointer"
              />
            </div>

            <div className="space-y-1.5">
              <div className="flex justify-between items-center text-[11px]">
                <span className="text-[#94A3B8]">Ratio:</span>
                <span className="text-[#38BDF8] font-bold">{params.compRatio.toFixed(1)}:1</span>
              </div>
              <input
                type="range"
                min="1.0"
                max="10.0"
                step="0.5"
                value={params.compRatio}
                onChange={(e) => onParamChange('compRatio', parseFloat(e.target.value))}
                className="w-full accent-[#38BDF8] bg-[#1A1D24] rounded h-1.5 cursor-pointer"
              />
            </div>

            <div className="space-y-1.5">
              <div className="flex justify-between items-center text-[11px]">
                <span className="text-[#94A3B8]">Attack Time:</span>
                <span className="text-white font-bold">{params.compAttackMs.toFixed(1)} ms</span>
              </div>
              <input
                type="range"
                min="0.1"
                max="50.0"
                step="0.5"
                value={params.compAttackMs}
                onChange={(e) => onParamChange('compAttackMs', parseFloat(e.target.value))}
                className="w-full accent-white bg-[#1A1D24] rounded h-1.5 cursor-pointer"
              />
            </div>

            <div className="space-y-1.5">
              <div className="flex justify-between items-center text-[11px]">
                <span className="text-[#94A3B8]">Release Time:</span>
                <span className="text-white font-bold">{params.compReleaseMs.toFixed(0)} ms</span>
              </div>
              <input
                type="range"
                min="10"
                max="500"
                step="10"
                value={params.compReleaseMs}
                onChange={(e) => onParamChange('compReleaseMs', parseFloat(e.target.value))}
                className="w-full accent-white bg-[#1A1D24] rounded h-1.5 cursor-pointer"
              />
            </div>
          </div>
        </div>

      </div>

    </div>
  );
};
