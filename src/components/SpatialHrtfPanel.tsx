import React, { useEffect, useRef } from 'react';
import { DspParameters } from '../types';
import { Layers, Cpu, Compass, Radio, Activity, Sparkles } from 'lucide-react';

interface SpatialHrtfPanelProps {
  params: DspParameters;
  onParamChange: (key: keyof DspParameters, value: any) => void;
}

export const SpatialHrtfPanel: React.FC<SpatialHrtfPanelProps> = ({ params, onParamChange }) => {
  const polarCanvasRef = useRef<HTMLCanvasElement | null>(null);

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

    </div>
  );
};
