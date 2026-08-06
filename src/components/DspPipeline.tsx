import React from 'react';
import { DspParameters } from '../types';
import { Cpu, Zap, Activity, Waves, Sliders, ShieldAlert, Sparkles } from 'lucide-react';

interface DspPipelineProps {
  params: DspParameters;
  onParamChange: (key: keyof DspParameters, value: number | boolean) => void;
}

export const DspPipeline: React.FC<DspPipelineProps> = ({ params }) => {
  return (
    <div className="space-y-6">
      
      {/* Intro Header */}
      <div className="bg-[#12141A] border border-[#2A2D35] rounded-xl p-5">
        <div className="flex flex-col lg:flex-row lg:items-center justify-between gap-4">
          <div>
            <h2 className="text-sm font-bold text-white font-mono flex items-center gap-2 uppercase tracking-wide">
              <Activity className="w-4 h-4 text-[#4ADE80]" />
              IVANNA-OMEGA-SUPREME v2.0 - Signal Processing Pipeline Architecture
            </h2>
            <p className="text-xs text-[#888] font-mono mt-1 max-w-3xl">
              100% ARMv8 NEON vectorization (<code className="text-[#4ADE80]">float32x4_t</code>, <code className="text-[#4ADE80]">int16x8_t</code>). Designed for zero heap allocations in the audio thread, sub-microsecond latency, L1 cache optimization, and phase-lock stability.
            </p>
          </div>
          <div className="flex items-center gap-3 font-mono text-xs">
            <div className="px-3 py-1.5 bg-[#0F1116] border border-[#1E2128] rounded">
              <span className="text-[#555] block text-[9px] font-bold uppercase">SAMPLE RATE</span>
              <span className="text-white font-bold">{params.sampleRate.toLocaleString()} Hz</span>
            </div>
            <div className="px-3 py-1.5 bg-[#0F1116] border border-[#1E2128] rounded">
              <span className="text-[#555] block text-[9px] font-bold uppercase">BLOCK SIZE</span>
              <span className="text-[#4ADE80] font-bold">{params.blockSize} frames</span>
            </div>
            <div className="px-3 py-1.5 bg-[#0F1116] border border-[#1E2128] rounded">
              <span className="text-[#555] block text-[9px] font-bold uppercase">ALIGNMENT</span>
              <span className="text-[#FB923C] font-bold">alignas(16)</span>
            </div>
          </div>
        </div>
      </div>

      {/* Nodes Interactive Diagram */}
      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-5 gap-4">
        
        {/* Node 1: TinyML Anti-Dolby YAMNet LSTM Predictor */}
        <div className="bg-[#12141A] border border-[#2A2D35] rounded-xl p-4 space-y-3 relative hover:border-[#4ADE80] transition-colors">
          <div className="flex items-center justify-between">
            <span className="text-[10px] font-mono font-bold px-2 py-0.5 rounded bg-[#1A1D23] text-[#78DCE8] border border-[#2A2D35]">
              STAGE 01
            </span>
            <ShieldAlert className="w-4 h-4 text-[#78DCE8]" />
          </div>
          <h3 className="text-xs font-bold font-mono text-white flex items-center gap-1.5">
            TinyML Anti-Dolby
          </h3>
          <p className="text-[11px] text-[#888] font-mono leading-relaxed">
            Quantized 8-unit LSTM inference in Q.7/Q.8 fixed-point evaluating RMS accumulation to predict listening fatigue & replaces legacy YAMNet.
          </p>
          <div className="pt-2 border-t border-[#1E2128] font-mono text-xs space-y-1.5">
            <div className="flex justify-between text-[#888]">
              <span>Fatigue Index:</span>
              <span className="text-[#78DCE8] font-bold">{(params.fatigueIndex * 100).toFixed(0)}%</span>
            </div>
            <div className="w-full bg-[#0F1116] rounded-full h-1.5 overflow-hidden border border-[#1E2128]">
              <div 
                className="bg-[#78DCE8] h-full transition-all duration-300"
                style={{ width: `${params.fatigueIndex * 100}%` }}
              />
            </div>
            <div className="flex justify-between text-[11px] text-[#888] pt-1">
              <span>Dynamic IIR Cut:</span>
              <span className="text-[#4ADE80] font-bold">Alpha {(params.iirAlpha).toFixed(2)}</span>
            </div>
          </div>
        </div>

        {/* Node 2: Evolutionary EQ */}
        <div className="bg-[#12141A] border border-[#2A2D35] rounded-xl p-4 space-y-3 relative hover:border-[#4ADE80] transition-colors">
          <div className="flex items-center justify-between">
            <span className="text-[10px] font-mono font-bold px-2 py-0.5 rounded bg-[#1A1D23] text-[#4ADE80] border border-[#2A2D35]">
              STAGE 02
            </span>
            <Sliders className="w-4 h-4 text-[#4ADE80]" />
          </div>
          <h3 className="text-xs font-bold font-mono text-white flex items-center gap-1.5">
            LM-CMA-ES FIR EQ
          </h3>
          <p className="text-[11px] text-[#888] font-mono leading-relaxed">
            256-Tap time-domain FIR filter driven by 512-band evolutionary genome with phase-smoothness penalization constraint.
          </p>
          <div className="pt-2 border-t border-[#1E2128] font-mono text-xs space-y-1.5">
            <div className="flex justify-between text-[#888]">
              <span>Taps / SIMD Unroll:</span>
              <span className="text-[#4ADE80] font-bold">256 / 4-Tap</span>
            </div>
            <div className="flex justify-between text-[#888]">
              <span>Genome Bands:</span>
              <span className="text-white">512 Bands</span>
            </div>
            <div className="flex justify-between text-[11px] text-[#888]">
              <span>Evolution Step:</span>
              <span className="text-[#FB923C] font-bold">{params.eqMutationRate.toFixed(2)}</span>
            </div>
          </div>
        </div>

        {/* Node 3: Psychoacoustic Masking */}
        <div className="bg-[#12141A] border border-[#2A2D35] rounded-xl p-4 space-y-3 relative hover:border-[#4ADE80] transition-colors">
          <div className="flex items-center justify-between">
            <span className="text-[10px] font-mono font-bold px-2 py-0.5 rounded bg-[#1A1D23] text-[#A9DC76] border border-[#2A2D35]">
              STAGE 03
            </span>
            <Waves className="w-4 h-4 text-[#A9DC76]" />
          </div>
          <h3 className="text-xs font-bold font-mono text-white flex items-center gap-1.5">
            Dynamic Masking
          </h3>
          <p className="text-[11px] text-[#888] font-mono leading-relaxed">
            Envelope follower-driven upward expander preventing transient crushing during high-energy bass peaks.
          </p>
          <div className="pt-2 border-t border-[#1E2128] font-mono text-xs space-y-1.5">
            <div className="flex justify-between text-[#888]">
              <span>Attack / Release:</span>
              <span className="text-[#A9DC76] font-bold">1ms / 10ms</span>
            </div>
            <div className="flex justify-between text-[#888]">
              <span>Expander Ratio:</span>
              <span className="text-white">1:1.5 Upward</span>
            </div>
            <div className="flex justify-between text-[#888]">
              <span>Threshold:</span>
              <span className="text-[#A9DC76] font-bold">-40 dBFS</span>
            </div>
          </div>
        </div>

        {/* Node 4: 3D Binaural HRTF */}
        <div className="bg-[#12141A] border border-[#2A2D35] rounded-xl p-4 space-y-3 relative hover:border-[#4ADE80] transition-colors">
          <div className="flex items-center justify-between">
            <span className="text-[10px] font-mono font-bold px-2 py-0.5 rounded bg-[#1A1D23] text-[#3B82F6] border border-[#2A2D35]">
              STAGE 04
            </span>
            <Cpu className="w-4 h-4 text-[#3B82F6]" />
          </div>
          <h3 className="text-xs font-bold font-mono text-white flex items-center gap-1.5">
            3D Binaural HRTF
          </h3>
          <p className="text-[11px] text-[#888] font-mono leading-relaxed">
            2x2 crosstalk matrix convolution implementing Rayleigh spherical head model ITD and ILD spatial acoustic simulation.
          </p>
          <div className="pt-2 border-t border-[#1E2128] font-mono text-xs space-y-1.5">
            <div className="flex justify-between text-[#888]">
              <span>Crosstalk Gain:</span>
              <span className="text-[#3B82F6] font-bold">{(params.crosstalkGain * 100).toFixed(0)}%</span>
            </div>
            <div className="flex justify-between text-[#888]">
              <span>ITD Delay:</span>
              <span className="text-white">{params.hrtfDelayMs.toFixed(2)} ms</span>
            </div>
            <div className="flex justify-between text-[#888]">
              <span>HRTF Taps:</span>
              <span className="text-[#3B82F6] font-bold">128 Taps</span>
            </div>
          </div>
        </div>

        {/* Node 5: Golden Ear GAN */}
        <div className={`bg-[#12141A] border rounded-xl p-4 space-y-3 relative transition-all ${
          params.goldenEarEnabled
            ? 'border-[#FB923C]'
            : 'border-[#2A2D35] opacity-75'
        }`}>
          <div className="flex items-center justify-between">
            <span className={`text-[10px] font-mono font-bold px-2 py-0.5 rounded border ${
              params.goldenEarEnabled
                ? 'bg-[#1A1D23] text-[#FB923C] border-[#FB923C]/50'
                : 'bg-[#0F1116] text-[#555] border-[#1E2128]'
            }`}>
              STAGE 05
            </span>
            <Sparkles className={`w-4 h-4 ${params.goldenEarEnabled ? 'text-[#FB923C]' : 'text-[#555]'}`} />
          </div>
          <h3 className="text-xs font-bold font-mono text-white flex items-center gap-1.5">
            Golden Ear GAN
          </h3>
          <p className="text-[11px] text-[#888] font-mono leading-relaxed">
            Chebyshev non-linear harmonic exciter H2(x)=2x^2-1 and H3(x)=4x^3-3x with Newton-Raphson fast_tanh_neon soft clipping.
          </p>
          <div className="pt-2 border-t border-[#1E2128] font-mono text-xs space-y-1.5">
            <div className="flex justify-between text-[#888]">
              <span>Harmonic Drive:</span>
              <span className="text-[#FB923C] font-bold">{params.goldenEarDrive.toFixed(2)}x</span>
            </div>
            <div className="flex justify-between text-[#888]">
              <span>Exciter Mix:</span>
              <span className="text-white">{(params.goldenEarMix * 100).toFixed(0)}%</span>
            </div>
            <div className="flex justify-between text-[#888]">
              <span>Tanh Method:</span>
              <span className="text-[#4ADE80] font-bold">1-Iter Newton</span>
            </div>
          </div>
        </div>

      </div>

      {/* Deep Technical Explanations */}
      <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
        
        {/* Math & NEON Features */}
        <div className="bg-[#12141A] border border-[#2A2D35] rounded-xl p-5 space-y-3 font-mono text-xs">
          <h3 className="text-xs font-bold text-white flex items-center gap-2 tracking-wide uppercase">
            <Zap className="w-4 h-4 text-[#4ADE80]" />
            Mathematical Precision & ARM NEON Optimizations
          </h3>
          <ul className="space-y-2 text-[#888] text-xs leading-relaxed">
            <li className="flex items-start gap-2">
              <span className="text-[#4ADE80] font-bold">1.</span>
              <span>
                <strong className="text-white">SIMD Fused Multiply-Add (FMA):</strong> All convolution and matrix updates utilize <code className="bg-[#0F1116] px-1 py-0.5 rounded border border-[#1E2128] text-[#4ADE80]">vmlaq_f32</code> and <code className="bg-[#0F1116] px-1 py-0.5 rounded border border-[#1E2128] text-[#4ADE80]">vld1q_f32</code>, processing 4 32-bit floats per instruction cycle.
              </span>
            </li>
            <li className="flex items-start gap-2">
              <span className="text-[#4ADE80] font-bold">2.</span>
              <span>
                <strong className="text-white">Fast Tanh Approximation:</strong> Evaluates f(x) = x*(27 + x^2) / (27 + 9*x^2) using NEON reciprocal estimate <code className="bg-[#0F1116] px-1 py-0.5 rounded text-[#FB923C]">vrecpeq_f32</code> followed by one Newton-Raphson iteration <code className="bg-[#0F1116] px-1 py-0.5 rounded text-[#FB923C]">vrecpsq_f32</code>.
              </span>
            </li>
            <li className="flex items-start gap-2">
              <span className="text-[#4ADE80] font-bold">3.</span>
              <span>
                <strong className="text-white">Zero Quantization Error:</strong> TinyML LSTM weights and states operate in int8/int16 domain, mapping cell sums to fixed point Q.7/Q.8 scales without floating-point underflow.
              </span>
            </li>
          </ul>
        </div>

        {/* Zero-Allocation Rules */}
        <div className="bg-[#12141A] border border-[#2A2D35] rounded-xl p-5 space-y-3 font-mono text-xs">
          <h3 className="text-xs font-bold text-white flex items-center gap-2 tracking-wide uppercase">
            <Cpu className="w-4 h-4 text-[#4ADE80]" />
            Zero-Allocation & L1 Cache Guarantees
          </h3>
          <ul className="space-y-2 text-[#888] text-xs leading-relaxed">
            <li className="flex items-start gap-2">
              <span className="text-[#4ADE80] font-bold">1.</span>
              <span>
                <strong className="text-white">Heap-Free Audio Thread:</strong> Zero <code className="text-[#FF6188]">malloc</code>, <code className="text-[#FF6188]">new</code>, or resizing <code className="text-[#FF6188]">std::vector</code> inside <code className="text-[#4ADE80]">process()</code>. All historical sample frames reside in static ring buffers.
              </span>
            </li>
            <li className="flex items-start gap-2">
              <span className="text-[#4ADE80] font-bold">2.</span>
              <span>
                <strong className="text-white">Strict 16-Byte Alignment:</strong> Every audio block buffer uses <code className="bg-[#0F1116] px-1 py-0.5 rounded text-[#78DCE8]">alignas(16)</code> matching 128-bit NEON SIMD registers to eliminate unaligned vector load penalties.
              </span>
            </li>
            <li className="flex items-start gap-2">
              <span className="text-[#4ADE80] font-bold">3.</span>
              <span>
                <strong className="text-white">LM-CMA-ES Phase Lock:</strong> The evolutionary optimizer enforces penalization on adjacent genome deltas, maintaining smooth frequency transitions and zero phase smearing in FIR filters.
              </span>
            </li>
          </ul>
        </div>

      </div>

    </div>
  );
};
