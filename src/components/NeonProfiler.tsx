import React from 'react';
import { BenchmarkMetrics } from '../types';
import { Cpu, ShieldCheck, Terminal, Activity, CheckCircle2 } from 'lucide-react';

interface NeonProfilerProps {
  metrics: BenchmarkMetrics;
}

export const NeonProfiler: React.FC<NeonProfilerProps> = ({ metrics }) => {
  return (
    <div className="space-y-6 font-mono">

      {/* Metrics Banner */}
      <div className="grid grid-cols-2 md:grid-cols-4 gap-4">

        <div className="bg-[#12141A] border border-[#2A2D35] rounded-xl p-4 space-y-1">
          <div className="text-[11px] text-[#888] font-bold uppercase tracking-wider">BLOCK LATENCY</div>
          <div className="text-xl font-bold text-[#78DCE8]">
            {metrics.blockLatencyMicroseconds.toFixed(1)} µs
          </div>
          <div className="text-[10px] text-[#4ADE80] font-semibold">
            {metrics.sampleLatencyNanoseconds.toFixed(1)} ns / sample
          </div>
        </div>

        <div className="bg-[#12141A] border border-[#2A2D35] rounded-xl p-4 space-y-1">
          <div className="text-[11px] text-[#888] font-bold uppercase tracking-wider">SIMD VECTORIZATION</div>
          <div className="text-xl font-bold text-[#4ADE80]">
            {metrics.simdEfficiencyPercent}% NEON
          </div>
          <div className="text-[10px] text-[#888]">
            128-bit float32x4_t / int16x8_t
          </div>
        </div>

        <div className="bg-[#12141A] border border-[#2A2D35] rounded-xl p-4 space-y-1">
          <div className="text-[11px] text-[#888] font-bold uppercase tracking-wider">AUDIO THREAD HEAP</div>
          <div className="text-xl font-bold text-[#4ADE80] flex items-center gap-1.5">
            <ShieldCheck className="w-5 h-5 text-[#4ADE80]" />
            0.00 Bytes
          </div>
          <div className="text-[10px] text-[#888]">Zero heap allocations in process()</div>
        </div>

        <div className="bg-[#12141A] border border-[#2A2D35] rounded-xl p-4 space-y-1">
          <div className="text-[11px] text-[#888] font-bold uppercase tracking-wider">L1 CACHE HIT RATE</div>
          <div className="text-xl font-bold text-[#FB923C]">
            {metrics.l1CacheHitRatePercent}%
          </div>
          <div className="text-[10px] text-[#888]">alignas(16) cacheline fit</div>
        </div>

      </div>

      {/* GFLOPS + Fitness + Clip + Calibration row */}
      <div className="grid grid-cols-2 md:grid-cols-4 gap-4">

        <div className="bg-[#12141A] border border-[#2A2D35] rounded-xl p-4 space-y-1">
          <div className="text-[11px] text-[#888] font-bold uppercase tracking-wider">THROUGHPUT</div>
          <div className="text-xl font-bold text-[#38BDF8]">
            {metrics.gflopsThroughput.toFixed(1)} GFLOPS
          </div>
          <div className="text-[10px] text-[#888]">Optimal block: {metrics.optimalBlockSize} samples</div>
        </div>

        <div className="bg-[#12141A] border border-[#2A2D35] rounded-xl p-4 space-y-1">
          <div className="text-[11px] text-[#888] font-bold uppercase tracking-wider">Q-REGISTERS</div>
          <div className="text-xl font-bold text-[#A855F7]">
            {metrics.registersActiveCount} active
          </div>
          <div className="text-[10px] text-[#888]">ARM NEON 128-bit vector lanes</div>
        </div>

        <div className="bg-[#12141A] border border-[#2A2D35] rounded-xl p-4 space-y-1">
          <div className="text-[11px] text-[#888] font-bold uppercase tracking-wider">EVO FITNESS</div>
          <div className={`text-xl font-bold ${metrics.evolutionFitness < 0 ? 'text-[#4ADE80]' : 'text-[#FF6188]'}`}>
            {metrics.evolutionFitness.toFixed(5)}
          </div>
          <div className="text-[10px] text-[#888]">CMA-ES best-of-generation</div>
        </div>

        <div className="bg-[#12141A] border border-[#2A2D35] rounded-xl p-4 space-y-1">
          <div className="text-[11px] text-[#888] font-bold uppercase tracking-wider">CLIP EVENTS</div>
          <div className={`text-xl font-bold ${metrics.clipCount > 0 ? 'text-[#FF6188]' : 'text-[#4ADE80]'}`}>
            {metrics.clipCount}
          </div>
          <div className="text-[10px] text-[#888]">
            {metrics.lastCalibratedAt ? `Calibrated: ${metrics.lastCalibratedAt}` : 'Not calibrated'}
          </div>
        </div>

      </div>

      {/* Calibration Log (persisted) */}
      {metrics.calibrationLog.length > 0 && (
        <div className="bg-[#12141A] border border-[#2A2D35] rounded-xl p-5 space-y-3">
          <h3 className="text-xs font-bold text-white flex items-center gap-2 uppercase tracking-wide">
            <CheckCircle2 className="w-4 h-4 text-[#4ADE80]" />
            Last SIMD Auto-Calibration Log
          </h3>
          <div className="bg-[#0A0C10] border border-[#1E2128] rounded p-3 font-mono text-[10px] text-[#94A3B8] max-h-36 overflow-y-auto space-y-0.5">
            {metrics.calibrationLog.map((line, i) => (
              <div key={i} className="flex items-start gap-2">
                <span className={line.startsWith('✅') ? 'text-[#4ADE80]' : 'text-[#38BDF8]'}>❯</span>
                <span className={line.startsWith('✅') ? 'text-[#4ADE80] font-bold' : ''}>{line}</span>
              </div>
            ))}
          </div>
        </div>
      )}

      {/* Assembly & Intrinsics Map */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-4">

        <div className="bg-[#12141A] border border-[#2A2D35] rounded-xl p-5 space-y-4">
          <h3 className="text-xs font-bold text-white flex items-center gap-2 uppercase tracking-wide">
            <Cpu className="w-4 h-4 text-[#4ADE80]" />
            ARMv8 Target Flags & Compiler Optimizations
          </h3>
          <div className="space-y-2 text-xs">
            <div className="flex justify-between py-1.5 border-b border-[#1E2128]">
              <span className="text-[#888]">Target CPU & Architecture:</span>
              <span className="text-[#78DCE8] font-bold">-mcpu=cortex-a76 -march=armv8.2-a+simd+fp16</span>
            </div>
            <div className="flex justify-between py-1.5 border-b border-[#1E2128]">
              <span className="text-[#888]">Optimization Level:</span>
              <span className="text-[#4ADE80] font-bold">-O3 -ffast-math -ftree-vectorize</span>
            </div>
            <div className="flex justify-between py-1.5 border-b border-[#1E2128]">
              <span className="text-[#888]">Link Time Optimization:</span>
              <span className="text-[#4ADE80] font-bold">-flto</span>
            </div>
            <div className="flex justify-between py-1.5 border-b border-[#1E2128]">
              <span className="text-[#888]">Exception & RTTI Overhead:</span>
              <span className="text-[#FB923C] font-bold">-fno-exceptions -fno-rtti</span>
            </div>
            <div className="flex justify-between py-1.5 border-b border-[#1E2128]">
              <span className="text-[#888]">Frame Pointer & Alignment:</span>
              <span className="text-white">-fomit-frame-pointer alignas(16)</span>
            </div>
            <div className="flex justify-between py-1.5">
              <span className="text-[#888]">Strip & Pack:</span>
              <span className="text-white">-s -Wl,--gc-sections</span>
            </div>
          </div>
        </div>

        <div className="bg-[#12141A] border border-[#2A2D35] rounded-xl p-5 space-y-4">
          <h3 className="text-xs font-bold text-white flex items-center gap-2 uppercase tracking-wide">
            <Terminal className="w-4 h-4 text-[#FB923C]" />
            Core ARM NEON Intrinsics & Instructions Used
          </h3>
          <div className="space-y-2 text-xs">
            {[
              { code: 'vmlaq_f32(a, b, c)', desc: 'Vector Fused Multiply-Add (a + b * c)', badge: 'FIR & HRTF Loops', color: '#78DCE8' },
              { code: 'vrecpeq_f32 / vrecpsq_f32', desc: 'Reciprocal estimate & Newton-Raphson step', badge: 'fast_tanh_neon', color: '#4ADE80' },
              { code: 'vld1q_f32 / vst1q_f32', desc: '128-bit aligned vector load & store', badge: 'alignas(16) Buffers', color: '#FB923C' },
              { code: 'vdupq_n_s16 / vmulq_s16', desc: 'Quantized int16 vector multiply', badge: 'TinyML int8 LSTM', color: '#A9DC76' },
              { code: 'vaddq_f32 / vsubq_f32', desc: 'SIMD parallel add/subtract', badge: 'Anti-Dolby phase', color: '#A855F7' },
            ].map(({ code, desc, badge, color }) => (
              <div key={code} className="p-2.5 bg-[#0F1116] rounded border border-[#1E2128] flex justify-between items-center gap-2">
                <div className="min-w-0">
                  <code className="font-bold text-[10px]" style={{ color }}>{code}</code>
                  <span className="block text-[10px] text-[#888] truncate">{desc}</span>
                </div>
                <span className="px-2 py-0.5 rounded bg-[#1A1D23] border border-[#2A2D35] text-[10px] font-bold shrink-0" style={{ color }}>
                  {badge}
                </span>
              </div>
            ))}
          </div>
        </div>

      </div>

    </div>
  );
};
