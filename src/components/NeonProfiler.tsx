import React from 'react';
import { BenchmarkMetrics } from '../types';
import { Cpu, ShieldCheck, Terminal } from 'lucide-react';

interface NeonProfilerProps {
  metrics: BenchmarkMetrics;
}

export const NeonProfiler: React.FC<NeonProfilerProps> = ({ metrics }) => {
  return (
    <div className="space-y-6 font-mono">
      
      {/* Metrics Banner */}
      <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
        
        <div className="bg-[#12141A] border border-[#2A2D35] rounded-xl p-4 space-y-1">
          <div className="text-[11px] text-[#888] font-bold uppercase tracking-wider">1024-FRAME LATENCY</div>
          <div className="text-xl font-bold text-[#78DCE8]">
            {metrics.blockLatencyMicroseconds.toFixed(1)} µs
          </div>
          <div className="text-[10px] text-[#4ADE80] font-semibold">
            {(metrics.sampleLatencyNanoseconds).toFixed(1)} ns / sample
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
          <div className="text-[10px] text-[#888]">
            Zero heap allocations in process()
          </div>
        </div>

        <div className="bg-[#12141A] border border-[#2A2D35] rounded-xl p-4 space-y-1">
          <div className="text-[11px] text-[#888] font-bold uppercase tracking-wider">L1 CACHE HIT RATE</div>
          <div className="text-xl font-bold text-[#FB923C]">
            {metrics.l1CacheHitRatePercent}%
          </div>
          <div className="text-[10px] text-[#888]">
            alignas(16) cacheline fit
          </div>
        </div>

      </div>

      {/* Assembly & Intrinsics Map */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-4">
        
        {/* Compiler & Target Architecture Specs */}
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
              <span className="text-[#888]">Link Time Optimization (LTO):</span>
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
          </div>
        </div>

        {/* NEON Intrinsics Breakdown */}
        <div className="bg-[#12141A] border border-[#2A2D35] rounded-xl p-5 space-y-4">
          <h3 className="text-xs font-bold text-white flex items-center gap-2 uppercase tracking-wide">
            <Terminal className="w-4 h-4 text-[#FB923C]" />
            Core ARM NEON Intrinsics & Instructions Used
          </h3>
          <div className="space-y-2 text-xs">
            <div className="p-2.5 bg-[#0F1116] rounded border border-[#1E2128] flex justify-between items-center">
              <div>
                <code className="text-[#78DCE8] font-bold">vmlaq_f32(a, b, c)</code>
                <span className="block text-[10px] text-[#888]">Vector Fused Multiply-Add (a + b * c)</span>
              </div>
              <span className="px-2 py-0.5 rounded bg-[#1A1D23] text-[#78DCE8] border border-[#2A2D35] text-[10px] font-bold">
                FIR & HRTF Loops
              </span>
            </div>

            <div className="p-2.5 bg-[#0F1116] rounded border border-[#1E2128] flex justify-between items-center">
              <div>
                <code className="text-[#4ADE80] font-bold">vrecpeq_f32 / vrecpsq_f32</code>
                <span className="block text-[10px] text-[#888]">Reciprocal estimate & Newton-Raphson step</span>
              </div>
              <span className="px-2 py-0.5 rounded bg-[#1A1D23] text-[#4ADE80] border border-[#2A2D35] text-[10px] font-bold">
                fast_tanh_neon
              </span>
            </div>

            <div className="p-2.5 bg-[#0F1116] rounded border border-[#1E2128] flex justify-between items-center">
              <div>
                <code className="text-[#FB923C] font-bold">vld1q_f32 / vst1q_f32</code>
                <span className="block text-[10px] text-[#888]">128-bit aligned vector load & store</span>
              </div>
              <span className="px-2 py-0.5 rounded bg-[#1A1D23] text-[#FB923C] border border-[#2A2D35] text-[10px] font-bold">
                alignas(16) Buffers
              </span>
            </div>

            <div className="p-2.5 bg-[#0F1116] rounded border border-[#1E2128] flex justify-between items-center">
              <div>
                <code className="text-[#A9DC76] font-bold">vdupq_n_s16 / vmulq_s16</code>
                <span className="block text-[10px] text-[#888]">Quantized int16 vector multiply</span>
              </div>
              <span className="px-2 py-0.5 rounded bg-[#1A1D23] text-[#A9DC76] border border-[#2A2D35] text-[10px] font-bold">
                TinyML LSTM int8
              </span>
            </div>
          </div>
        </div>

      </div>

    </div>
  );
};

