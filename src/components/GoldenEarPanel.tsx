import React, { useEffect, useRef } from 'react';
import { DspParameters } from '../types';
import { Sparkles, Zap, Sliders, Cpu, Activity } from 'lucide-react';

interface GoldenEarPanelProps {
  params: DspParameters;
  onParamChange: (key: keyof DspParameters, value: any) => void;
}

export const GoldenEarPanel: React.FC<GoldenEarPanelProps> = ({ params, onParamChange }) => {
  const transferCanvasRef = useRef<HTMLCanvasElement | null>(null);

  // Render Non-Linear Transfer Curve
  useEffect(() => {
    const canvas = transferCanvasRef.current;
    if (canvas) {
      const ctx = canvas.getContext('2d');
      if (ctx) {
        const w = canvas.width;
        const h = canvas.height;

        ctx.fillStyle = '#0A0C10';
        ctx.fillRect(0, 0, w, h);

        // Center axes
        ctx.strokeStyle = '#1A1D24';
        ctx.lineWidth = 1;

        ctx.beginPath();
        ctx.moveTo(0, h / 2);
        ctx.lineTo(w, h / 2);
        ctx.stroke();

        ctx.beginPath();
        ctx.moveTo(w / 2, 0);
        ctx.lineTo(w / 2, h);
        ctx.stroke();

        // Transfer curve plotting
        ctx.strokeStyle = params.goldenEarEnabled ? '#F97316' : '#64748B';
        ctx.lineWidth = 2.5;
        ctx.shadowBlur = params.goldenEarEnabled ? 10 : 0;
        ctx.shadowColor = '#F97316';
        ctx.beginPath();

        for (let xPx = 0; xPx <= w; xPx++) {
          const xNorm = (xPx / w) * 2.0 - 1.0; // [-1.0, 1.0]

          let yNorm = xNorm;

          if (params.goldenEarEnabled) {
            const drv = xNorm * params.goldenEarDrive;
            const h2 = 2.0 * (drv * drv) - 1.0;
            const h3 = 4.0 * (drv * drv * drv) - 3.0 * drv;
            yNorm = xNorm + (h2 * 0.12 + h3 * 0.08) * params.goldenEarMix;

            // Soft tanh clip
            yNorm = Math.tanh(yNorm);
          }

          const yPx = h / 2 - yNorm * (h * 0.4);

          if (xPx === 0) ctx.moveTo(xPx, yPx);
          else ctx.lineTo(xPx, yPx);
        }

        ctx.stroke();
        ctx.shadowBlur = 0;
      }
    }
  }, [params.goldenEarEnabled, params.goldenEarDrive, params.goldenEarMix]);

  return (
    <div className="space-y-6 font-mono text-xs">
      
      {/* Banner */}
      <div className="bg-[#10131A] border border-[#232936] rounded-xl p-5 shadow-lg">
        <div className="flex flex-col lg:flex-row lg:items-center justify-between gap-4">
          <div>
            <div className="flex items-center space-x-2">
              <span className="text-[10px] font-bold px-2.5 py-0.5 rounded bg-[#2D1B14] text-[#F97316] border border-[#48281A]">
                CHEBYSHEV HARMONIC GAN
              </span>
              <h2 className="text-sm font-bold text-white flex items-center gap-2 uppercase tracking-wide">
                Golden Ear GAN & Soft-Clipping Exciter Engine
              </h2>
            </div>
            <p className="text-xs text-[#64748B] mt-1.5 max-w-3xl">
              Non-linear polynomial harmonic generation H2(x)=2x^2-1 and H3(x)=4x^3-3x coupled with Newton-Raphson 1-iteration <code className="text-[#F97316]">fast_tanh_neon</code> soft saturation.
            </p>
          </div>

          <button
            onClick={() => onParamChange('goldenEarEnabled', !params.goldenEarEnabled)}
            className={`px-4 py-2 rounded-lg text-xs font-bold transition-all border ${
              params.goldenEarEnabled
                ? 'bg-[#2D1B14] border-[#F97316] text-[#F97316]'
                : 'bg-[#12151C] border-[#1E2330] text-[#64748B]'
            }`}
          >
            {params.goldenEarEnabled ? 'GOLDEN EAR ACTIVE' : 'BYPASS'}
          </button>
        </div>
      </div>

      {/* Main Grid */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-5">
        
        {/* Transfer Curve Plot */}
        <div className="bg-[#10131A] border border-[#232936] rounded-xl p-5 space-y-4">
          <div className="flex items-center justify-between border-b border-[#1E2330] pb-3">
            <div className="flex items-center gap-2">
              <Sparkles className="w-4 h-4 text-[#F97316]" />
              <h3 className="font-bold text-white uppercase text-xs">Non-Linear Transfer Function Curve</h3>
            </div>
            <span className="text-[10px] text-[#F97316] font-bold">
              Drive: {params.goldenEarDrive.toFixed(2)}x
            </span>
          </div>

          <div className="relative rounded-lg overflow-hidden border border-[#1E2128] bg-[#0A0C10]">
            <canvas
              ref={transferCanvasRef}
              width={500}
              height={300}
              className="w-full h-72 object-cover"
            />
          </div>
        </div>

        {/* Controls */}
        <div className="bg-[#10131A] border border-[#232936] rounded-xl p-5 space-y-4">
          <div className="border-b border-[#1E2330] pb-3">
            <h3 className="font-bold text-white uppercase text-xs">Harmonic Exciter Tuning</h3>
          </div>

          {/* Drive */}
          <div className="space-y-1.5">
            <div className="flex justify-between text-xs">
              <span className="text-[#94A3B8]">Harmonic Drive:</span>
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

          {/* Harmonic Mix */}
          <div className="space-y-1.5">
            <div className="flex justify-between text-xs">
              <span className="text-[#94A3B8]">Exciter Mix Ratio:</span>
              <span className="text-white font-bold">{(params.goldenEarMix * 100).toFixed(0)}%</span>
            </div>
            <input
              type="range"
              min="0.0"
              max="0.5"
              step="0.02"
              value={params.goldenEarMix}
              onChange={(e) => onParamChange('goldenEarMix', parseFloat(e.target.value))}
              className="w-full accent-[#F97316] bg-[#1A1D24] rounded h-1.5 cursor-pointer"
            />
          </div>

          {/* Harmonic Gain Boost */}
          <div className="space-y-1.5 pt-2 border-t border-[#1E2330]">
            <div className="flex justify-between text-xs">
              <span className="text-[#94A3B8]">Harmonic Boost:</span>
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

          {/* Math details */}
          <div className="p-3 bg-[#0A0C10] rounded-lg border border-[#1A1D24] space-y-1.5 text-[11px]">
            <span className="text-[#64748B] block font-bold">NEON FAST TANH MATH:</span>
            <code className="text-[#4ADE80] block text-[10px]">f(x) = x*(27 + x^2) / (27 + 9*x^2)</code>
            <p className="text-[10px] text-[#64748B]">
              Evaluated with <code className="text-[#38BDF8]">vrecpeq_f32</code> and 1 Newton-Raphson refinement <code className="text-[#38BDF8]">vrecpsq_f32</code>.
            </p>
          </div>

        </div>

      </div>

    </div>
  );
};
