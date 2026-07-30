import React from 'react';
import { Cpu, Zap, Activity, ShieldCheck, Terminal, Layers } from 'lucide-react';

interface HeaderProps {
  activeTab: 'pipeline' | 'visualizer' | 'benchmarks' | 'code' | 'termux';
  setActiveTab: (tab: 'pipeline' | 'visualizer' | 'benchmarks' | 'code' | 'termux') => void;
  goldenEarEnabled: boolean;
  onToggleGoldenEar: () => void;
}

export const Header: React.FC<HeaderProps> = ({
  activeTab,
  setActiveTab,
  goldenEarEnabled,
  onToggleGoldenEar,
}) => {
  return (
    <header id="header-container" className="border-b border-[#2A2D35] bg-[#12141A] sticky top-0 z-50 text-[#E0E0E0]">
      {/* Top macOS-style Status Bar */}
      <div className="flex flex-col md:flex-row items-center justify-between px-4 sm:px-6 py-2.5 bg-[#0F1116] border-b border-[#1E2128] gap-3">
        <div className="flex items-center space-x-3 w-full md:w-auto justify-between md:justify-start">
          <div className="flex items-center space-x-2">
            <div className="w-3 h-3 rounded-full bg-[#FF5F56] shadow-sm shadow-[#FF5F56]/50"></div>
            <div className="w-3 h-3 rounded-full bg-[#FFBD2E] shadow-sm shadow-[#FFBD2E]/50"></div>
            <div className="w-3 h-3 rounded-full bg-[#27C93F] shadow-sm shadow-[#27C93F]/50"></div>
            <span className="ml-3 text-[11px] font-mono text-[#888] tracking-widest uppercase font-semibold">
              IVANNA-FUSION v2.0 // DSP_ARCHITECT_MODE
            </span>
          </div>

          {/* Golden Ear Quick Toggle */}
          <button
            id="golden-ear-toggle-btn"
            onClick={onToggleGoldenEar}
            className={`flex items-center space-x-1.5 px-2.5 py-1 rounded text-[11px] font-mono font-bold transition-all border ${
              goldenEarEnabled
                ? 'bg-[#1E2229] border-[#FB923C] text-[#FB923C]'
                : 'bg-[#161920] border-[#2A2D35] text-[#888] hover:text-[#CCC]'
            }`}
          >
            <Zap className={`w-3 h-3 ${goldenEarEnabled ? 'text-[#FB923C] animate-pulse' : ''}`} />
            <span>GAN: {goldenEarEnabled ? 'ACTIVE' : 'BYPASS'}</span>
          </button>
        </div>

        <div className="flex items-center space-x-6 text-xs font-mono">
          <div className="flex flex-col items-end hidden sm:flex">
            <span className="text-[9px] text-[#555] uppercase tracking-wider font-bold">Target Architecture</span>
            <span className="text-[11px] text-[#4ADE80] font-mono font-semibold">aarch64-v8a (NEON Enabled)</span>
          </div>

          <div className="flex items-center space-x-2 px-3 py-1 bg-[#1A1D23] border border-[#2A2D35] rounded text-[11px]">
            <ShieldCheck className="w-3.5 h-3.5 text-[#4ADE80]" />
            <span className="text-[#888]">Heap:</span>
            <span className="text-[#4ADE80] font-bold">0.00 B</span>
          </div>

          <button
            onClick={() => setActiveTab('termux')}
            className="px-3 py-1 bg-[#1E2229] border border-[#3A3F4B] text-[#CCC] hover:text-white text-[11px] font-mono font-bold rounded hover:bg-[#2A2F3A] transition-colors"
          >
            BUILD & RELEASE
          </button>
        </div>
      </div>

      {/* Main Header Brand & Navigation */}
      <div className="max-w-7xl mx-auto px-4 sm:px-6 py-3 flex flex-col md:flex-row md:items-center justify-between gap-4">
        <div className="flex items-center space-x-3">
          <div className="p-2 rounded-lg bg-[#161920] border border-[#2A2D35] text-[#4ADE80]">
            <Cpu className="w-5 h-5 animate-pulse" />
          </div>
          <div>
            <div className="flex items-center space-x-2">
              <h1 className="text-base font-bold text-white font-mono tracking-wide">
                IVANNA-OMEGA-SUPREME <span className="text-[#4ADE80] text-xs">v2.0</span>
              </h1>
              <span className="text-[10px] px-2 py-0.5 rounded bg-[#1A1D23] text-[#4ADE80] border border-[#2A2D35] font-mono font-bold">
                C++17 Kernel
              </span>
            </div>
            <p className="text-[11px] text-[#888] font-mono">
              TinyML Anti-Dolby Neural DSP • Magisk Android Daemon
            </p>
          </div>
        </div>

        {/* Workspace Navigation Tabs */}
        <nav id="header-nav-tabs" className="flex space-x-1 overflow-x-auto font-mono text-xs pb-1 md:pb-0">
          <button
            id="tab-pipeline-btn"
            onClick={() => setActiveTab('pipeline')}
            className={`flex items-center space-x-2 px-3 py-2 rounded text-xs font-medium transition-all border ${
              activeTab === 'pipeline'
                ? 'bg-[#1A1D23] border-[#4ADE80] text-[#4ADE80] font-bold'
                : 'bg-[#12141A] border-[#1E2128] text-[#888] hover:text-[#CCC] hover:bg-[#161920]'
            }`}
          >
            <Layers className="w-3.5 h-3.5" />
            <span>Topology Pipeline</span>
          </button>

          <button
            id="tab-visualizer-btn"
            onClick={() => setActiveTab('visualizer')}
            className={`flex items-center space-x-2 px-3 py-2 rounded text-xs font-medium transition-all border ${
              activeTab === 'visualizer'
                ? 'bg-[#1A1D23] border-[#4ADE80] text-[#4ADE80] font-bold'
                : 'bg-[#12141A] border-[#1E2128] text-[#888] hover:text-[#CCC] hover:bg-[#161920]'
            }`}
          >
            <Activity className="w-3.5 h-3.5" />
            <span>Audio & FFT Spectrum</span>
          </button>

          <button
            id="tab-benchmarks-btn"
            onClick={() => setActiveTab('benchmarks')}
            className={`flex items-center space-x-2 px-3 py-2 rounded text-xs font-medium transition-all border ${
              activeTab === 'benchmarks'
                ? 'bg-[#1A1D23] border-[#4ADE80] text-[#4ADE80] font-bold'
                : 'bg-[#12141A] border-[#1E2128] text-[#888] hover:text-[#CCC] hover:bg-[#161920]'
            }`}
          >
            <Cpu className="w-3.5 h-3.5" />
            <span>NEON Profiler</span>
          </button>

          <button
            id="tab-code-btn"
            onClick={() => setActiveTab('code')}
            className={`flex items-center space-x-2 px-3 py-2 rounded text-xs font-medium transition-all border ${
              activeTab === 'code'
                ? 'bg-[#1A1D23] border-[#4ADE80] text-[#4ADE80] font-bold'
                : 'bg-[#12141A] border-[#1E2128] text-[#888] hover:text-[#CCC] hover:bg-[#161920]'
            }`}
          >
            <Terminal className="w-3.5 h-3.5" />
            <span>C++ & Magisk Code</span>
          </button>

          <button
            id="tab-termux-btn"
            onClick={() => setActiveTab('termux')}
            className={`flex items-center space-x-2 px-3 py-2 rounded text-xs font-medium transition-all border ${
              activeTab === 'termux'
                ? 'bg-[#1A1D23] border-[#FB923C] text-[#FB923C] font-bold'
                : 'bg-[#12141A] border-[#1E2128] text-[#888] hover:text-[#CCC] hover:bg-[#161920]'
            }`}
          >
            <Terminal className="w-3.5 h-3.5 text-[#FB923C]" />
            <span>Termux Installer</span>
          </button>
        </nav>
      </div>
    </header>
  );
};

