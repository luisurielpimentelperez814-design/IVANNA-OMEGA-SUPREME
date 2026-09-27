import React, { useEffect, useRef, useState } from 'react';
import { DspParameters } from '../types';
import { Compass } from 'lucide-react';

interface SpatialHrtfPanelProps {
  params: DspParameters;
  onParamChange: <K extends keyof DspParameters>(key: K, value: DspParameters[K]) => void;
}

const MASTER_RIR_ARCHETYPES = [
  { idx: 51,  file: 'rir_0051.wav', name: 'Studio Control Room (ITU-R BS.1116)', rt60: 0.340, drr: 10.31, c80: 16.66, iaccEarly: 0.784, iaccLate: 0.218, wet: 0.22, route: 'Boot Default · Wired AUX & USB DAC', width: 1.25, angle: 32, delay: 0.32, crosstalk: 0.22 },
  { idx: 122, file: 'rir_0122.wav', name: 'Intimate Mastering Chamber',          rt60: 0.451, drr: 8.62,  c80: 13.48, iaccEarly: 0.752, iaccLate: 0.224, wet: 0.25, route: 'Acoustic & Vocal Reference',         width: 1.35, angle: 38, delay: 0.36, crosstalk: 0.26 },
  { idx: 169, file: 'rir_0169.wav', name: 'Symphonic Concert Hall',              rt60: 0.860, drr: 5.84,  c80: 8.91,  iaccEarly: 0.718, iaccLate: 0.218, wet: 0.30, route: 'Orchestral & Cinema 3D',             width: 1.55, angle: 45, delay: 0.44, crosstalk: 0.30 },
  { idx: 81,  file: 'rir_0081.wav', name: 'Open Speaker Projection Room',        rt60: 0.613, drr: 7.40,  c80: 11.20, iaccEarly: 0.741, iaccLate: 0.231, wet: 0.16, route: 'Speaker Route Auto-Calibration',     width: 1.45, angle: 40, delay: 0.28, crosstalk: 0.16 },
  { idx: 63,  file: 'rir_0063.wav', name: 'Bluetooth Tight Anti-Codec Room',     rt60: 0.293, drr: 9.85,  c80: 15.92, iaccEarly: 0.812, iaccLate: 0.245, wet: 0.18, route: 'Bluetooth A2DP / LDAC Route',        width: 1.15, angle: 28, delay: 0.26, crosstalk: 0.18 },
];

export const SpatialHrtfPanel: React.FC<SpatialHrtfPanelProps> = ({ params, onParamChange }) => {
  const polarCanvasRef = useRef<HTMLCanvasElement | null>(null);
  const [selectedRirIdx, setSelectedRirIdx] = useState<number>(51);
  const [bootMode, setBootMode] = useState<'ROOT_MAGISK' | 'NON_ROOT_JNI'>('ROOT_MAGISK');
  const activeRoom = MASTER_RIR_ARCHETYPES.find((r) => r.idx === selectedRirIdx) ?? MASTER_RIR_ARCHETYPES[0];

  // Render 3D Polar Soundstage
  useEffect(() => {
    let animId: number;
    let pulse = 0;

    const render = () => {
      pulse += 0.05;
      const canvas = polarCanvasRef.current;
      if (canvas) {
        const ctx = canvas.getContext('2d');
        if (ctx) {
          const w = canvas.width;
          const h = canvas.height;
          const cx = w / 2;
          const cy = h / 2 + 20;

          ctx.fillStyle = '#0A0C10';
          ctx.fillRect(0, 0, w, h);

          // Polar Grid Rings
          ctx.strokeStyle = '#1A1D24';
          ctx.lineWidth = 1;
          for (let r = 40; r <= 160; r += 40) {
            ctx.beginPath();
            ctx.arc(cx, cy, r, 0, Math.PI * 2);
            ctx.stroke();
          }

          // Center Head Icon
          ctx.fillStyle = '#182230';
          ctx.strokeStyle = '#A855F7';
          ctx.lineWidth = 2;
          ctx.beginPath();
          ctx.arc(cx, cy, 22, 0, Math.PI * 2);
          ctx.fill();
          ctx.stroke();

          // Left / Right Ears
          ctx.fillStyle = '#A855F7';
          ctx.fillRect(cx - 27, cy - 6, 5, 12);
          ctx.fillRect(cx + 22, cy - 6, 5, 12);

          // Calculate Speaker Positions based on Spatial Angle
          const rad = (params.spatialAngleDeg * Math.PI) / 180;
          const dist = 140 * params.spatialWidth;

          const lx = cx - dist * Math.sin(rad);
          const ly = cy - dist * Math.cos(rad);

          const rx = cx + dist * Math.sin(rad);
          const ry = cy - dist * Math.cos(rad);

          // Draw Speaker Drivers
          ctx.fillStyle = '#38BDF8';
          ctx.shadowBlur = 12;
          ctx.shadowColor = '#38BDF8';

          ctx.beginPath();
          ctx.arc(lx, ly, 10, 0, Math.PI * 2);
          ctx.fill();

          ctx.beginPath();
          ctx.arc(rx, ry, 10, 0, Math.PI * 2);
          ctx.fill();

          // Soundwave propagation arcs
          ctx.strokeStyle = 'rgba(56, 189, 248, 0.4)';
          ctx.lineWidth = 1.5;
          const waveR = (pulse * 25) % 80;

          ctx.beginPath();
          ctx.arc(lx, ly, waveR, 0, Math.PI * 2);
          ctx.stroke();

          ctx.beginPath();
          ctx.arc(rx, ry, waveR, 0, Math.PI * 2);
          ctx.stroke();

          // Crosstalk Matrix Vectors (if crosstalk > 0)
          if (params.crosstalkGain > 0) {
            ctx.strokeStyle = 'rgba(168, 85, 247, 0.5)';
            ctx.lineWidth = 1;
            ctx.setLineDash([4, 4]);

            // Left speaker to Right ear
            ctx.beginPath();
            ctx.moveTo(lx, ly);
            ctx.lineTo(cx + 22, cy);
            ctx.stroke();

            // Right speaker to Left ear
            ctx.beginPath();
            ctx.moveTo(rx, ry);
            ctx.lineTo(cx - 27, cy);
            ctx.stroke();

            ctx.setLineDash([]);
          }

          ctx.shadowBlur = 0;
        }
      }

      animId = requestAnimationFrame(render);
    };

    render();

    return () => {
      cancelAnimationFrame(animId);
    };
  }, [params.spatialAngleDeg, params.spatialWidth, params.crosstalkGain]);

  return (
    <div className="space-y-6 font-mono text-xs">
      
      {/* Banner */}
      <div className="bg-[#10131A] border border-[#232936] rounded-xl p-5 shadow-lg">
        <div className="flex flex-col lg:flex-row lg:items-center justify-between gap-4">
          <div>
            <div className="flex items-center space-x-2">
              <span className="text-[10px] font-bold px-2.5 py-0.5 rounded bg-[#23182E] text-[#A855F7] border border-[#352246]">
                3D HRTF RAYLEIGH MATRIX
              </span>
              <h2 className="text-sm font-bold text-white flex items-center gap-2 uppercase tracking-wide">
                Binaural 3D Spatial Stage & Interaural Delay Engine
              </h2>
            </div>
            <p className="text-xs text-[#64748B] mt-1.5 max-w-3xl">
              2x2 crosstalk matrix convolution implementing Rayleigh spherical head model ITD (Interaural Time Delay) and ILD (Level Difference).
            </p>
          </div>

          <button
            onClick={() => onParamChange('hrtfEnabled', !params.hrtfEnabled)}
            className={`px-4 py-2 rounded-lg text-xs font-bold transition-all border ${
              params.hrtfEnabled
                ? 'bg-[#23182E] border-[#A855F7] text-[#A855F7]'
                : 'bg-[#12151C] border-[#1E2330] text-[#64748B]'
            }`}
          >
            {params.hrtfEnabled ? '3D HRTF ON' : '3D HRTF BYPASS'}
          </button>
        </div>
      </div>

      {/* Grid */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-5">
        
        {/* Radar Soundstage */}
        <div className="bg-[#10131A] border border-[#232936] rounded-xl p-5 space-y-4">
          <div className="flex items-center justify-between border-b border-[#1E2330] pb-3">
            <div className="flex items-center gap-2">
              <Compass className="w-4 h-4 text-[#A855F7]" />
              <h3 className="font-bold text-white uppercase text-xs">Acoustic Soundstage Polar Radar</h3>
            </div>
            <span className="text-[10px] text-[#A855F7] font-bold">
              Angle: {params.spatialAngleDeg}°
            </span>
          </div>

          <div className="relative rounded-lg overflow-hidden border border-[#1E2128] bg-[#0A0C10]">
            <canvas
              ref={polarCanvasRef}
              width={500}
              height={300}
              className="w-full h-72 object-cover"
            />
          </div>
        </div>

        {/* Sliders */}
        <div className="bg-[#10131A] border border-[#232936] rounded-xl p-5 space-y-4">
          <div className="border-b border-[#1E2330] pb-3">
            <h3 className="font-bold text-white uppercase text-xs">Spatial Fine-Tuning Controls</h3>
          </div>

          {/* Azimuth Angle */}
          <div className="space-y-1.5">
            <div className="flex justify-between text-xs">
              <span className="text-[#94A3B8]">Azimuth Angle:</span>
              <span className="text-[#A855F7] font-bold">{params.spatialAngleDeg}°</span>
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
            <div className="flex justify-between text-xs">
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

          {/* Crosstalk Gain */}
          <div className="space-y-1.5">
            <div className="flex justify-between text-xs">
              <span className="text-[#94A3B8]">Crosstalk Matrix Gain:</span>
              <span className="text-[#38BDF8] font-bold">{(params.crosstalkGain * 100).toFixed(0)}%</span>
            </div>
            <input
              type="range"
              min="0.0"
              max="0.6"
              step="0.02"
              value={params.crosstalkGain}
              onChange={(e) => onParamChange('crosstalkGain', parseFloat(e.target.value))}
              className="w-full accent-[#38BDF8] bg-[#1A1D24] rounded h-1.5 cursor-pointer"
            />
          </div>

          {/* ITD Delay */}
          <div className="space-y-1.5">
            <div className="flex justify-between text-xs">
              <span className="text-[#94A3B8]">Interaural Time Delay (ITD):</span>
              <span className="text-white font-bold">{params.hrtfDelayMs.toFixed(2)} ms</span>
            </div>
            <input
              type="range"
              min="0.0"
              max="1.0"
              step="0.05"
              value={params.hrtfDelayMs}
              onChange={(e) => onParamChange('hrtfDelayMs', parseFloat(e.target.value))}
              className="w-full accent-white bg-[#1A1D24] rounded h-1.5 cursor-pointer"
            />
          </div>

          {/* Spatial Wet Eta */}
          <div className="space-y-1.5 pt-2 border-t border-[#1E2330]">
            <div className="flex justify-between text-xs">
              <span className="text-[#94A3B8]">Wet Mix Eta:</span>
              <span className="text-[#A855F7] font-bold">{(params.spatialWetEta * 100).toFixed(0)}%</span>
            </div>
            <input
              type="range"
              min="0.0"
              max="1.0"
              step="0.02"
              value={params.spatialWetEta}
              onChange={(e) => onParamChange('spatialWetEta', parseFloat(e.target.value))}
              className="w-full accent-[#A855F7] bg-[#1A1D24] rounded h-1.5 cursor-pointer"
            />
          </div>

        </div>

      </div>

      {/* Joint SOFA (255 AES69) + SAF (Riemannian 7-D) + RIR (200 Rooms) Master Knowledge Panel */}
      <div className="bg-[#10131A] border border-[#232936] rounded-xl p-5 space-y-4">
        <div className="flex flex-wrap items-center justify-between gap-3 border-b border-[#1E2330] pb-3">
          <div>
            <h3 className="font-bold text-white uppercase text-xs tracking-wider">
              Joint SOFA (255 AES69) · SAF (7-D Riemannian Manifold) · RIR (200 Rooms) Master Calibration
            </h3>
            <p className="text-[11px] text-[#94A3B8] mt-0.5">
              Zero-heap 64B-aligned C++23 tensors baked into <code className="text-[#38BDF8]">SofaSafRirMasterKnowledge.hpp</code> — active at t=0ms boot
            </p>
          </div>
          <div className="flex items-center gap-2">
            <button
              type="button"
              onClick={() => setBootMode('ROOT_MAGISK')}
              className={`px-3 py-1 rounded text-xs font-mono border transition ${
                bootMode === 'ROOT_MAGISK'
                  ? 'bg-[#A855F7]/20 border-[#A855F7] text-white'
                  : 'bg-[#0A0C10] border-[#232936] text-[#94A3B8]'
              }`}
            >
              Ruta B · Root Magisk (audioserver)
            </button>
            <button
              type="button"
              onClick={() => setBootMode('NON_ROOT_JNI')}
              className={`px-3 py-1 rounded text-xs font-mono border transition ${
                bootMode === 'NON_ROOT_JNI'
                  ? 'bg-[#38BDF8]/20 border-[#38BDF8] text-white'
                  : 'bg-[#0A0C10] border-[#232936] text-[#94A3B8]'
              }`}
            >
              Ruta A · Non-Root JNI (In-Process)
            </button>
          </div>
        </div>

        <div className="grid grid-cols-1 lg:grid-cols-3 gap-4">
          {/* Archetype Selector */}
          <div className="lg:col-span-2 space-y-2">
            <div className="text-[11px] text-[#94A3B8] uppercase font-semibold">
              5 Golden Master Trained BRIR Room Archetypes (Click to Audition)
            </div>
            <div className="grid grid-cols-1 sm:grid-cols-2 gap-2">
              {MASTER_RIR_ARCHETYPES.map((room) => {
                const isSel = room.idx === selectedRirIdx;
                return (
                  <button
                    key={room.idx}
                    type="button"
                    onClick={() => {
                      setSelectedRirIdx(room.idx);
                      onParamChange('spatialWidth', room.width);
                      onParamChange('spatialAngleDeg', room.angle);
                      onParamChange('hrtfDelayMs', room.delay);
                      onParamChange('crosstalkGain', room.crosstalk);
                      onParamChange('spatialWetEta', room.wet);
                    }}
                    className={`text-left p-3 rounded-lg border transition ${
                      isSel
                        ? 'bg-[#181E2C] border-[#38BDF8] shadow-[0_0_12px_rgba(56,189,248,0.15)]'
                        : 'bg-[#0A0C10] border-[#1E2330] hover:border-[#334155]'
                    }`}
                  >
                    <div className="flex items-center justify-between">
                      <span className="text-xs font-bold text-white">{room.name}</span>
                      <span className="text-[10px] font-mono px-1.5 py-0.5 rounded bg-[#1E293B] text-[#38BDF8]">
                        #{room.idx} · {room.file}
                      </span>
                    </div>
                    <div className="text-[10px] text-[#94A3B8] mt-1">{room.route}</div>
                    <div className="flex flex-wrap gap-3 mt-2 text-[10px] font-mono text-[#CBD5E1]">
                      <span>RT60: <strong className="text-[#A855F7]">{room.rt60.toFixed(3)}s</strong></span>
                      <span>DRR: <strong className="text-[#38BDF8]">{room.drr.toFixed(2)}dB</strong></span>
                      <span>C80: <strong className="text-emerald-400">{room.c80.toFixed(2)}dB</strong></span>
                      <span>IACC_E/L: <strong>{room.iaccEarly.toFixed(2)}/{room.iaccLate.toFixed(2)}</strong></span>
                    </div>
                  </button>
                );
              })}
            </div>
          </div>

          {/* Active SOFA-SAF-RIR Coupling Readout */}
          <div className="bg-[#0A0C10] border border-[#1E2330] rounded-lg p-3.5 space-y-2.5 font-mono text-xs">
            <div className="text-[11px] font-bold text-[#38BDF8] uppercase border-b border-[#1E2330] pb-1.5">
              Active Φ_SAF-Room^∞ Geodesic State
            </div>
            <div className="flex justify-between">
              <span className="text-[#94A3B8]">Execution Path:</span>
              <span className="text-emerald-400 font-bold">
                {bootMode === 'ROOT_MAGISK' ? 'AudioFlinger GlobalEffect' : 'JNI Lock-Free Engine'}
              </span>
            </div>
            <div className="flex justify-between">
              <span className="text-[#94A3B8]">Active BRIR Room:</span>
              <span className="text-white">#{activeRoom.idx} ({activeRoom.file})</span>
            </div>
            <div className="flex justify-between">
              <span className="text-[#94A3B8]">SOFA Manifold Basis:</span>
              <span className="text-[#A855F7]">214 Subj · 128-Tap C¹</span>
            </div>
            <div className="flex justify-between">
              <span className="text-[#94A3B8]">Golden Latent q*[0..2]:</span>
              <span className="text-[#38BDF8]">[+4.01e-3, 0.0, +5.50e-3]</span>
            </div>
            <div className="flex justify-between">
              <span className="text-[#94A3B8]">Early Pinna Pre-Emphasis:</span>
              <span className="text-white">+1.084x (0–1.3 ms)</span>
            </div>
            <div className="flex justify-between">
              <span className="text-[#94A3B8]">Late Allpass Decorrel:</span>
              <span className="text-white">0.642 (IACC_late={activeRoom.iaccLate.toFixed(3)})</span>
            </div>
            <div className="pt-1 text-[10px] text-[#64748B] leading-relaxed">
              Pre-seeded at <code className="text-[#94A3B8]">EffectCreate</code> &amp; <code className="text-[#94A3B8]">PersistedStateRestorer</code>: zero cold-boot silence, zero impulse spikes, bit-exact across Root &amp; Non-Root.
            </div>
          </div>
        </div>
      </div>

    </div>
  );
};
