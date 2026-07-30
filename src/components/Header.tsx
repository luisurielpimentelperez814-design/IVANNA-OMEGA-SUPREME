import React from 'react';
import { Cpu, Zap, Activity, ShieldCheck, Terminal, Layers, Sliders, Waves, Sparkles, Power, Radio, RotateCcw } from 'lucide-react';
import { DspParameters } from '../types';

interface HeaderProps {
  activeTab: 'master' | 'tinyml' | 'evo_eq' | 'spatial' | 'golden_ear' | 'visualizer' | 'benchmarks' | 'code';
  setActiveTab: (tab: 'master' | 'tinyml' | 'evo_eq' | 'spatial' | 'golden_ear' | 'visualizer' | 'benchmarks' | 'code') => void;
  params: DspParameters;
  onParamChange: (key: keyof DspParameters, value: any) => void;
  onApplyPreset: (presetName: DspParameters['activePreset']) => void;
}

export const Header: React.FC<HeaderProps> = ({
  activeTab,
  setActiveTab,
  params,
  onParamChange,
  onApplyPreset,
}) => {
  return (
    <header id="header-container" className="border-b border-[#2A2D35] bg-[#101217] sticky top-0 z-50 text-[#E0E0E0] shadow-2xl">
      {/* Top Engineering Status Bar */}
      <div className="flex flex-col lg:flex-row items-center justify-between px-4 sm:px-6 py-2 bg-[#0A0C10] border-b border-[#1A1D24] gap-2 text-xs font-mono">
        
        {/* macOS Window Control & System Status */}
        <div className="flex items-center space-x-3 w-full lg:w-auto justify-between lg:justify-start">
          <div className="flex items-center space-x-2">
            <div className="w-3 h-3 rounded-full bg-[#FF5F56] shadow-sm shadow-[#FF5F56]/50"></div>
            <div className="w-3 h-3 rounded-full bg-[#FFBD2E] shadow-sm shadow-[#FFBD2E]/50"></div>
            <div className="w-3 h-3 rounded-full bg-[#27C93F] shadow-sm shadow-[#27C93F]/50"></div>
            <span className="ml-2 text-[10px] text-[#7E8B9B] tracking-widest uppercase font-semibold">
              IVANNA-OMEGA-SUPREME // ANTI-DOLBY_NEURAL_KERNEL
            </span>
          </div>

          {/* Master Bypass Toggle Button */}
          <button
            onClick={() => onParamChange('masterBypass', !params.masterBypass)}
            className={`flex items-center space-x-1.5 px-3 py-1 rounded text-[11px] font-mono font-bold transition-all border ${
              !params.masterBypass
                ? 'bg-[#18261E] border-[#4ADE80] text-[#4ADE80] shadow-sm shadow-[#4ADE80]/20'
                : 'bg-[#2A181A] border-[#FF6188] text-[#FF6188] animate-pulse'
            }`}
          >
            <Power className="w-3 h-3" />
            <span>DSP KERNEL: {!params.masterBypass ? 'ACTIVE' : 'BYPASS'}</span>
          </button>
        </div>

        {/* Preset Selector Badges */}
        <div className="flex items-center space-x-1.5 overflow-x-auto py-1 lg:py-0 text-[11px]">
          <span className="text-[#64748B] font-bold text-[10px] uppercase mr-1">Preset:</span>
          {(
            [
              { id: 'anti_dolby_extreme', label: 'Anti-Dolby Max', color: 'border-[#38BDF8] text-[#38BDF8]' },
              { id: 'audiophile', label: 'Audiophile 3D', color: 'border-[#A855F7] text-[#A855F7]' },
              { id: 'bass_head', label: 'Bass Harmonic', color: 'border-[#F97316] text-[#F97316]' },
              { id: 'vocal_protect', label: 'Vocal Protect', color: 'border-[#4ADE80] text-[#4ADE80]' },
              { id: 'gaming_spatial', label: 'Gaming 3D', color: 'border-[#F59E0B] text-[#F59E0B]' },
              { id: 'evo_cma_es', label: 'Evolutive CMA', color: 'border-[#EC4899] text-[#EC4899]' },
              { id: 'custom', label: 'Custom', color: 'border-[#64748B] text-[#94A3B8]' },
            ] as const
          ).map((p) => (
            <button
              key={p.id}
              onClick={() => onApplyPreset(p.id)}
              className={`px-2 py-0.5 rounded text-[10px] font-bold uppercase transition-all border shrink-0 ${
                params.activePreset === p.id
                  ? 'bg-[#1E2330] font-black underline shadow-sm'
                  : 'bg-[#12151C] border-[#1E2330] text-[#64748B] hover:text-[#CBD5E1]'
              } ${params.activePreset === p.id ? p.color : ''}`}
            >
              {p.label}
            </button>
          ))}
        </div>

        {/* Target Specs & Heap Zero Monitor */}
        <div className="flex items-center space-x-4">
          <div className="flex items-center space-x-1.5 text-[11px] text-[#A0AEC0]">
            <span className="text-[#64748B]">ARCH:</span>
            <span className="text-[#38BDF8] font-bold">aarch64-v8a</span>
          </div>

          <div className="flex items-center space-x-1.5 px-2.5 py-0.5 bg-[#141822] border border-[#232936] rounded text-[11px]">
            <ShieldCheck className="w-3.5 h-3.5 text-[#4ADE80]" />
            <span className="text-[#64748B]">Heap Alloc:</span>
            <span className="text-[#4ADE80] font-bold">0.0 B</span>
          </div>

          <button
            onClick={() => setActiveTab('code')}
            className="px-2.5 py-1 bg-[#1A202C] border border-[#2D3748] text-[#E2E8F0] hover:text-white text-[10px] font-bold rounded hover:bg-[#2D3748] transition-colors uppercase tracking-wider"
          >
            Termux Patch
          </button>
        </div>

      </div>

      {/* Main Header Brand Title & Navigation Tabs */}
      <div className="max-w-7xl mx-auto px-4 sm:px-6 py-3 flex flex-col md:flex-row md:items-center justify-between gap-4">
        
        {/* Brand & Kernel Badge */}
        <div className="flex items-center space-x-3">
          <div className="p-2 rounded-lg bg-[#141A24] border border-[#232D3F] text-[#38BDF8]">
            <Cpu className="w-5 h-5 animate-pulse" />
          </div>
          <div>
            <div className="flex items-center space-x-2">
              <h1 className="text-base font-bold text-white font-mono tracking-wide flex items-center gap-2">
                IVANNA-OMEGA-SUPREME <span className="text-[#38BDF8] text-xs">v2.0</span>
              </h1>
              <span className="text-[10px] px-2 py-0.5 rounded bg-[#182230] text-[#38BDF8] border border-[#243346] font-mono font-bold">
                Lock-Free SPSC DSP Kernel
              </span>
            </div>
            <p className="text-[11px] text-[#64748B] font-mono">
              TinyML ConvNeXt Audio Classifier • 512-Band CMA-ES FIR EQ • 3D HRTF Stage
            </p>
          </div>
        </div>

        {/* Primary Tab Navigation */}
        <nav id="header-nav-tabs" className="flex items-center space-x-1 overflow-x-auto font-mono text-xs pb-1 md:pb-0 scrollbar-none">
          
          <button
            onClick={() => setActiveTab('master')}
            className={`flex items-center space-x-1.5 px-3 py-2 rounded text-xs transition-all border shrink-0 ${
              activeTab === 'master'
                ? 'bg-[#182230] border-[#38BDF8] text-[#38BDF8] font-bold'
                : 'bg-[#101217] border-[#1E2330] text-[#64748B] hover:text-[#CBD5E1] hover:bg-[#141822]'
            }`}
          >
            <Sliders className="w-3.5 h-3.5" />
            <span>Master Control</span>
          </button>

          <button
            onClick={() => setActiveTab('tinyml')}
            className={`flex items-center space-x-1.5 px-3 py-2 rounded text-xs transition-all border shrink-0 ${
              activeTab === 'tinyml'
                ? 'bg-[#182230] border-[#38BDF8] text-[#38BDF8] font-bold'
                : 'bg-[#101217] border-[#1E2330] text-[#64748B] hover:text-[#CBD5E1] hover:bg-[#141822]'
            }`}
          >
            <Activity className="w-3.5 h-3.5 text-[#38BDF8]" />
            <span>TinyML ConvNeXt</span>
          </button>

          <button
            onClick={() => setActiveTab('evo_eq')}
            className={`flex items-center space-x-1.5 px-3 py-2 rounded text-xs transition-all border shrink-0 ${
              activeTab === 'evo_eq'
                ? 'bg-[#182230] border-[#4ADE80] text-[#4ADE80] font-bold'
                : 'bg-[#101217] border-[#1E2330] text-[#64748B] hover:text-[#CBD5E1] hover:bg-[#141822]'
            }`}
          >
            <Waves className="w-3.5 h-3.5 text-[#4ADE80]" />
            <span>512-Band CMA-ES</span>
          </button>

          <button
            onClick={() => setActiveTab('spatial')}
            className={`flex items-center space-x-1.5 px-3 py-2 rounded text-xs transition-all border shrink-0 ${
              activeTab === 'spatial'
                ? 'bg-[#182230] border-[#A855F7] text-[#A855F7] font-bold'
                : 'bg-[#101217] border-[#1E2330] text-[#64748B] hover:text-[#CBD5E1] hover:bg-[#141822]'
            }`}
          >
            <Layers className="w-3.5 h-3.5 text-[#A855F7]" />
            <span>3D HRTF Stage</span>
          </button>

          <button
            onClick={() => setActiveTab('golden_ear')}
            className={`flex items-center space-x-1.5 px-3 py-2 rounded text-xs transition-all border shrink-0 ${
              activeTab === 'golden_ear'
                ? 'bg-[#182230] border-[#F97316] text-[#F97316] font-bold'
                : 'bg-[#101217] border-[#1E2330] text-[#64748B] hover:text-[#CBD5E1] hover:bg-[#141822]'
            }`}
          >
            <Sparkles className="w-3.5 h-3.5 text-[#F97316]" />
            <span>Golden Ear GAN</span>
          </button>

          <button
            onClick={() => setActiveTab('visualizer')}
            className={`flex items-center space-x-1.5 px-3 py-2 rounded text-xs transition-all border shrink-0 ${
              activeTab === 'visualizer'
                ? 'bg-[#182230] border-[#38BDF8] text-[#38BDF8] font-bold'
                : 'bg-[#101217] border-[#1E2330] text-[#64748B] hover:text-[#CBD5E1] hover:bg-[#141822]'
            }`}
          >
            <Radio className="w-3.5 h-3.5" />
            <span>FFT & Oscilloscope</span>
          </button>

          <button
            onClick={() => setActiveTab('benchmarks')}
            className={`flex items-center space-x-1.5 px-3 py-2 rounded text-xs transition-all border shrink-0 ${
              activeTab === 'benchmarks'
                ? 'bg-[#182230] border-[#38BDF8] text-[#38BDF8] font-bold'
                : 'bg-[#101217] border-[#1E2330] text-[#64748B] hover:text-[#CBD5E1] hover:bg-[#141822]'
            }`}
          >
            <Cpu className="w-3.5 h-3.5" />
            <span>NEON Profiler</span>
          </button>

          <button
            onClick={() => setActiveTab('code')}
            className={`flex items-center space-x-1.5 px-3 py-2 rounded text-xs transition-all border shrink-0 ${
              activeTab === 'code'
                ? 'bg-[#182230] border-[#F59E0B] text-[#F59E0B] font-bold'
                : 'bg-[#101217] border-[#1E2330] text-[#64748B] hover:text-[#CBD5E1] hover:bg-[#141822]'
            }`}
          >
            <Terminal className="w-3.5 h-3.5 text-[#F59E0B]" />
            <span>C++ Code & Termux</span>
          </button>

        </nav>

      </div>
    </header>
  );
};
