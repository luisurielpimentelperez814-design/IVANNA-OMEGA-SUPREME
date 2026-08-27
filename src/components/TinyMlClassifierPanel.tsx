import React from 'react';
import { DspParameters, TinyMlClassification } from '../types';
import { Activity, ShieldAlert, Cpu, Layers, ShieldCheck, Zap, Radio, BarChart2 } from 'lucide-react';

interface TinyMlClassifierPanelProps {
  params: DspParameters;
  classification: TinyMlClassification;
  onParamChange: (key: keyof DspParameters, value: any) => void;
}

export const TinyMlClassifierPanel: React.FC<TinyMlClassifierPanelProps> = ({
  params,
  classification,
  onParamChange,
}) => {
  return (
    <div className="space-y-6 font-mono text-xs">
      
      {/* Banner */}
      <div className="bg-[#10131A] border border-[#232936] rounded-xl p-5 shadow-lg">
        <div className="flex flex-col lg:flex-row lg:items-center justify-between gap-4">
          <div>
            <div className="flex items-center space-x-2">
              <span className="text-[10px] font-bold px-2.5 py-0.5 rounded bg-[#182230] text-[#38BDF8] border border-[#243346]">
                YAMNET REPLACEMENT KERNEL
              </span>
              <h2 className="text-sm font-bold text-white flex items-center gap-2 uppercase tracking-wide">
                TinyML ConvNeXt 1D INT8 Classifier & Lock-Free SPSC Ring Buffer
              </h2>
            </div>
            <p className="text-xs text-[#64748B] mt-1.5 max-w-3xl">
              Native C++17 <code className="text-[#38BDF8]">IvannaAudioClassifier</code> executing 32-band Log-Mel filterbanks, depthwise-separable 1D convolutions, and quantized INT8 softmax in under 8.2 microseconds.
            </p>
          </div>

          <div className="flex items-center gap-3">
            <div className="px-3 py-2 bg-[#0A0C10] border border-[#1A1D24] rounded-lg">
              <span className="text-[#64748B] block text-[9px] font-bold uppercase">SPSC RING BUFFER</span>
              <span className="text-[#38BDF8] font-bold text-sm">{classification.spscDepth} / 2048 Samples</span>
            </div>
            <div className="px-3 py-2 bg-[#0A0C10] border border-[#1A1D24] rounded-lg">
              <span className="text-[#64748B] block text-[9px] font-bold uppercase">INFERENCE LATENCY</span>
              <span className="text-[#4ADE80] font-bold text-sm">{classification.inferenceTimeUs.toFixed(1)} µs</span>
            </div>
          </div>
        </div>
      </div>

      {/* Grid Display */}
      <div className="grid grid-cols-1 lg:grid-cols-3 gap-5">
        
        {/* 1. Live Probabilities */}
        <div className="bg-[#10131A] border border-[#232936] rounded-xl p-5 space-y-4 lg:col-span-2">
          <div className="flex items-center justify-between border-b border-[#1E2330] pb-3">
            <div className="flex items-center gap-2">
              <BarChart2 className="w-4 h-4 text-[#38BDF8]" />
              <h3 className="font-bold text-white uppercase text-xs">Real-time Scene Softmax Probabilities</h3>
            </div>
            <div className="flex items-center space-x-2">
              <span className="text-[#64748B] text-[11px]">Dominant:</span>
              <span className="text-[10px] px-2.5 py-0.5 rounded bg-[#18261E] text-[#4ADE80] border border-[#23382B] font-bold uppercase">
                {classification.dominantClass}
              </span>
            </div>
          </div>

          <div className="space-y-3 pt-1">
            
            {/* Speech */}
            <div className="space-y-1">
              <div className="flex justify-between items-center text-xs">
                <span className="text-[#CBD5E1] font-semibold flex items-center gap-2">
                  <span className="w-2 h-2 rounded-full bg-[#38BDF8]"></span> Speech / Vocal Dialogue
                </span>
                <span className="text-[#38BDF8] font-bold">{(classification.speech * 100).toFixed(1)}%</span>
              </div>
              <div className="w-full bg-[#0A0C10] rounded-full h-2 overflow-hidden border border-[#1A1D24]">
                <div
                  className="bg-[#38BDF8] h-full transition-all duration-300"
                  style={{ width: `${classification.speech * 100}%` }}
                />
              </div>
            </div>

            {/* Music */}
            <div className="space-y-1">
              <div className="flex justify-between items-center text-xs">
                <span className="text-[#CBD5E1] font-semibold flex items-center gap-2">
                  <span className="w-2 h-2 rounded-full bg-[#A855F7]"></span> Music / Polyphonic Harmonics
                </span>
                <span className="text-[#A855F7] font-bold">{(classification.music * 100).toFixed(1)}%</span>
              </div>
              <div className="w-full bg-[#0A0C10] rounded-full h-2 overflow-hidden border border-[#1A1D24]">
                <div
                  className="bg-[#A855F7] h-full transition-all duration-300"
                  style={{ width: `${classification.music * 100}%` }}
                />
              </div>
            </div>

            {/* Transient */}
            <div className="space-y-1">
              <div className="flex justify-between items-center text-xs">
                <span className="text-[#CBD5E1] font-semibold flex items-center gap-2">
                  <span className="w-2 h-2 rounded-full bg-[#F97316]"></span> Transient / Impact Peaks
                </span>
                <span className="text-[#F97316] font-bold">{(classification.transient * 100).toFixed(1)}%</span>
              </div>
              <div className="w-full bg-[#0A0C10] rounded-full h-2 overflow-hidden border border-[#1A1D24]">
                <div
                  className="bg-[#F97316] h-full transition-all duration-300"
                  style={{ width: `${classification.transient * 100}%` }}
                />
              </div>
            </div>

            {/* Ambient */}
            <div className="space-y-1">
              <div className="flex justify-between items-center text-xs">
                <span className="text-[#CBD5E1] font-semibold flex items-center gap-2">
                  <span className="w-2 h-2 rounded-full bg-[#4ADE80]"></span> Ambient / Noise Floor
                </span>
                <span className="text-[#4ADE80] font-bold">{(classification.ambient * 100).toFixed(1)}%</span>
              </div>
              <div className="w-full bg-[#0A0C10] rounded-full h-2 overflow-hidden border border-[#1A1D24]">
                <div
                  className="bg-[#4ADE80] h-full transition-all duration-300"
                  style={{ width: `${classification.ambient * 100}%` }}
                />
              </div>
            </div>

          </div>

          {/* Quantization Specs */}
          <div className="grid grid-cols-2 sm:grid-cols-4 gap-3 pt-3 border-t border-[#1E2330] text-[11px]">
            <div>
              <span className="text-[#64748B] block text-[9px] font-bold">MEL BANDS</span>
              <span className="text-white font-bold">32 Bands Log2</span>
            </div>
            <div>
              <span className="text-[#64748B] block text-[9px] font-bold">QUANT SCALE</span>
              <span className="text-[#38BDF8] font-bold">Q.7 / Q.8 Fixed</span>
            </div>
            <div>
              <span className="text-[#64748B] block text-[9px] font-bold">CONV CHANNELS</span>
              <span className="text-white font-bold">16 Depthwise</span>
            </div>
            <div>
              <span className="text-[#64748B] block text-[9px] font-bold">MUTEX STATUS</span>
              <span className="text-[#4ADE80] font-bold">Lock-Free SPSC</span>
            </div>
          </div>
        </div>

        {/* 2. Acoustic Fatigue & IIR Damping */}
        <div className="bg-[#10131A] border border-[#232936] rounded-xl p-5 space-y-4">
          <div className="flex items-center justify-between border-b border-[#1E2330] pb-3">
            <div className="flex items-center gap-2">
              <ShieldAlert className="w-4 h-4 text-[#38BDF8]" />
              <h3 className="font-bold text-white uppercase text-xs">Acoustic Fatigue Mitigator</h3>
            </div>
          </div>

          <div className="space-y-3">
            <div className="flex justify-between items-center text-xs">
              <span className="text-[#94A3B8] font-semibold">Fatigue Index:</span>
              <span className="text-[#38BDF8] font-bold text-sm">{(params.fatigueIndex * 100).toFixed(0)}%</span>
            </div>
            <input
              type="range"
              min="0.0"
              max="1.0"
              step="0.02"
              value={params.fatigueIndex}
              onChange={(e) => {
                const fatigue = parseFloat(e.target.value);
                // Actualización atómica: ambos parámetros en un solo dispatch
                onParamChange('fatigueIndex', fatigue);
                // iirAlpha derivado: 1 - fatigue*0.4  (rango 1.0 → 0.6)
                onParamChange('iirAlpha', parseFloat((1.0 - fatigue * 0.4).toFixed(3)));
              }}
              className="w-full accent-[#38BDF8] bg-[#1A1D24] rounded h-1.5 cursor-pointer"
            />

            <div className="p-3 bg-[#0A0C10] rounded-lg border border-[#1A1D24] space-y-2">
              <div className="flex justify-between text-[11px]">
                <span className="text-[#64748B]">Dynamic IIR Alpha:</span>
                <span className="text-[#4ADE80] font-bold">{params.iirAlpha.toFixed(2)}</span>
              </div>
              <p className="text-[10px] text-[#64748B] leading-relaxed">
                Automatically adjusts 1st-order high-frequency rolloff to prevent ear fatigue during extended listening sessions without muddying vocal clarity.
              </p>
            </div>
          </div>
        </div>

      </div>

    </div>
  );
};
