import React, { useEffect, useRef } from 'react';
import { DspParameters, BenchmarkMetrics } from '../types';
import { Waves, Play, Square, Cpu, Activity, Sliders } from 'lucide-react';
import { usePersist } from '../usePersist';

// Declaración global para evitar error TS con window.cefQuery (WebView bridge)
declare global {
  interface Window {
    cefQuery?: (opts: { request: string; onSuccess?: (r: string) => void; onFailure?: (c: number, m: string) => void }) => void;
  }
}

interface EvolutionaryEqPanelProps {
  params: DspParameters;
  metrics: BenchmarkMetrics;
  onParamChange: (key: keyof DspParameters, value: any) => void;
}

export const EvolutionaryEqPanel: React.FC<EvolutionaryEqPanelProps> = ({
  params,
  metrics,
  onParamChange,
}) => {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);

  // ── Persistentes ────────────────────────────────────────────────────────────
  const [isEvoRunning, setIsEvoRunning] = usePersist<boolean>('evo_running', true);
  // bestFitness sincronizado con la fuente de verdad: metrics.evolutionFitness
  const [bestFitness, setBestFitness] = usePersist<number>('evo_bestFitness', metrics.evolutionFitness);

  // Mantén bestFitness en sync con la telemetría del motor (toma el mejor)
  useEffect(() => {
    setBestFitness((prev) =>
      metrics.evolutionFitness < prev ? metrics.evolutionFitness : prev
    );
  }, [metrics.evolutionFitness]); // eslint-disable-line

  // ── Animación canvas ────────────────────────────────────────────────────────
  useEffect(() => {
    let animId: number;
    let frame = 0;

    const render = () => {
      frame += 0.05;
      const canvas = canvasRef.current;
      if (canvas) {
        const ctx = canvas.getContext('2d');
        if (ctx) {
          const w = canvas.width;
          const h = canvas.height;

          ctx.fillStyle = '#0A0C10';
          ctx.fillRect(0, 0, w, h);

          // Grid
          ctx.strokeStyle = '#1A1D24';
          ctx.lineWidth = 1;
          for (let y = 0; y <= h; y += h / 4) {
            ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke();
          }
          for (let x = 0; x <= w; x += w / 8) {
            ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, h); ctx.stroke();
          }

          // 512-band FIR magnitude response
          ctx.strokeStyle = isEvoRunning ? '#4ADE80' : '#94A3B8';
          ctx.lineWidth = 2.5;
          ctx.shadowBlur = isEvoRunning ? 10 : 0;
          ctx.shadowColor = '#4ADE80';
          ctx.beginPath();

          const bands = 512;
          for (let i = 0; i < bands; i++) {
            const freqRatio = i / bands;
            const x = freqRatio * w;

            let db = Math.sin(freqRatio * Math.PI * 6 + frame) * 3.5
              + Math.cos(freqRatio * Math.PI * 14 - frame * 0.5) * 2.0;

            if (isEvoRunning) {
              db += (Math.random() - 0.5) * params.eqMutationRate * 12.0;
            }

            const y = h / 2 - (db / 12.0) * (h * 0.4);
            if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
          }

          ctx.stroke();
          ctx.shadowBlur = 0;
        }
      }

      animId = requestAnimationFrame(render);
    };

    render();
    return () => cancelAnimationFrame(animId);
  }, [isEvoRunning, params.eqMutationRate]);

  const handleToggleEvo = () => {
    const next = !isEvoRunning;
    setIsEvoRunning(next);
    if (window.cefQuery) {
      window.cefQuery({
        request: JSON.stringify({ action: 'toggle_cma_es', enabled: next }),
        onSuccess: () => console.log('CMA-ES toggled:', next),
        onFailure: (code, msg) => console.warn('cefQuery error', code, msg),
      });
    }
  };

  return (
    <div className="space-y-6 font-mono text-xs">

      {/* Banner */}
      <div className="bg-[#10131A] border border-[#232936] rounded-xl p-5 shadow-lg">
        <div className="flex flex-col lg:flex-row lg:items-center justify-between gap-4">
          <div>
            <div className="flex items-center space-x-2">
              <span className="text-[10px] font-bold px-2.5 py-0.5 rounded bg-[#18261E] text-[#4ADE80] border border-[#23382B]">
                512-BAND GENOME ENGINE
              </span>
              <h2 className="text-sm font-bold text-white flex items-center gap-2 uppercase tracking-wide">
                LM-CMA-ES Evolutionary FIR Equalizer
              </h2>
            </div>
            <p className="text-xs text-[#64748B] mt-1.5 max-w-3xl">
              256-Tap time-domain FIR filter driven by covariance matrix adaptation evolutionary strategy with smooth phase penalization constraints.
            </p>
          </div>

          <button
            onClick={handleToggleEvo}
            className={`flex items-center space-x-2 px-4 py-2 rounded-lg text-xs font-bold transition-all border ${
              isEvoRunning
                ? 'bg-[#18261E] border-[#4ADE80] text-[#4ADE80]'
                : 'bg-[#2A181A] border-[#FF6188] text-[#FF6188]'
            }`}
          >
            {isEvoRunning ? <Square className="w-3.5 h-3.5" /> : <Play className="w-3.5 h-3.5" />}
            <span>{isEvoRunning ? 'EVOLUTION RUNNING' : 'PAUSED'}</span>
          </button>
        </div>
      </div>

      {/* Plot */}
      <div className="bg-[#10131A] border border-[#232936] rounded-xl p-5 space-y-4">
        <div className="flex items-center justify-between border-b border-[#1E2330] pb-3">
          <div className="flex items-center gap-2">
            <Waves className="w-4 h-4 text-[#4ADE80]" />
            <h3 className="font-bold text-white uppercase text-xs">512-Band Evolutionary Magnitude Response</h3>
          </div>
          <div className="flex items-center space-x-4 text-[11px]">
            <span className="text-[#64748B]">Best Fitness:</span>
            <span className="text-[#4ADE80] font-bold">{bestFitness.toFixed(5)}</span>
          </div>
        </div>

        <div className="relative rounded-lg overflow-hidden border border-[#1E2128] bg-[#0A0C10]">
          <canvas ref={canvasRef} width={800} height={260} className="w-full h-60 object-cover" />
        </div>

        <div className="flex justify-between text-[10px] text-[#64748B] pt-1 font-bold">
          <span>20 Hz (Bass)</span>
          <span>250 Hz</span>
          <span>1,000 Hz (Mid)</span>
          <span>4,000 Hz</span>
          <span>20,000 Hz (Treble)</span>
        </div>
      </div>

      {/* Controls & Specs */}
      <div className="grid grid-cols-1 md:grid-cols-3 gap-5">

        <div className="bg-[#10131A] border border-[#232936] rounded-xl p-5 space-y-3">
          <div className="flex justify-between items-center text-xs">
            <span className="text-[#94A3B8] font-semibold">CMA-ES Step Size (σ):</span>
            <span className="text-[#4ADE80] font-bold">{params.eqMutationRate.toFixed(2)}</span>
          </div>
          <input
            type="range" min="0.02" max="0.30" step="0.01"
            value={params.eqMutationRate}
            onChange={(e) => onParamChange('eqMutationRate', parseFloat(e.target.value))}
            className="w-full accent-[#4ADE80] bg-[#1A1D24] rounded h-1.5 cursor-pointer"
          />
          <p className="text-[10px] text-[#64748B] leading-relaxed">
            Controls mutation distance per generation step. Lower values produce fine-grain acoustic calibration.
          </p>
        </div>

        <div className="bg-[#10131A] border border-[#232936] rounded-xl p-5 space-y-2 text-xs">
          <span className="text-[#64748B] font-bold uppercase text-[10px] block">FIR Filter Taps</span>
          <div className="text-lg font-bold text-white">256 Time-Domain Taps</div>
          <div className="text-[11px] text-[#4ADE80] font-semibold">
            NEON 4-Tap Vector Unroll (<code className="text-[#4ADE80]">vmlaq_f32</code>)
          </div>
        </div>

        <div className="bg-[#10131A] border border-[#232936] rounded-xl p-5 space-y-2 text-xs">
          <span className="text-[#64748B] font-bold uppercase text-[10px] block">CMA-ES Genome Population</span>
          <div className="text-lg font-bold text-white">λ = 4 Candidates</div>
          <div className="text-[11px] text-[#38BDF8] font-semibold">
            Smooth Phase Penalization Active
          </div>
        </div>

      </div>
    </div>
  );
};
