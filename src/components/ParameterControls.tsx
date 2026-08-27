import React from 'react';
import { DspParameters } from '../types';
import { Sliders, Zap, ShieldAlert, Cpu, Waves } from 'lucide-react';

interface ParameterControlsProps {
  params: DspParameters;
  onParamChange: (key: keyof DspParameters, value: number | boolean) => void;
}

export const ParameterControls: React.FC<ParameterControlsProps> = ({ params, onParamChange }) => {
  return (
    <div className="bg-[#12141A] border border-[#2A2D35] rounded-xl p-5 space-y-5 font-mono text-xs">
      <div className="flex items-center justify-between border-b border-[#1E2128] pb-3">
        <h3 className="text-xs font-bold text-white flex items-center gap-2 tracking-wide uppercase">
          <Sliders className="w-4 h-4 text-[#4ADE80]" />
          Interactive DSP Hardware Parameter Fine-Tuning
        </h3>
        <span className="text-[10px] text-[#888]">Live Audio Thread Control</span>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-4">
        
        {/* Golden Ear Controls */}
        <div className="space-y-3 bg-[#0F1116] p-4 rounded-lg border border-[#1E2128]">
          <div className="flex items-center justify-between">
            <span className="font-bold text-[#FB923C] flex items-center gap-1.5">
              <Zap className="w-3.5 h-3.5" />
              Golden Ear Drive
            </span>
            <span className="text-[#FB923C] font-bold">{params.goldenEarDrive.toFixed(2)}x</span>
          </div>
          <input
            type="range"
            min="1.0"
            max="3.0"
            step="0.05"
            value={params.goldenEarDrive}
            onChange={(e) => onParamChange('goldenEarDrive', parseFloat(e.target.value))}
            className="w-full accent-[#FB923C] bg-[#1A1D23] rounded h-1.5 cursor-pointer"
          />
          <div className="flex justify-between text-[10px] text-[#555]">
            <span>1.0x (Clean)</span>
            <span>3.0x (Saturated)</span>
          </div>

          <div className="pt-2 border-t border-[#1E2128]">
            <div className="flex items-center justify-between text-[11px]">
              <span className="text-[#888]">Harmonic Mix:</span>
              <span className="text-white font-bold">{(params.goldenEarMix * 100).toFixed(0)}%</span>
            </div>
            <input
              type="range"
              min="0.0"
              max="0.5"
              step="0.02"
              value={params.goldenEarMix}
              onChange={(e) => onParamChange('goldenEarMix', parseFloat(e.target.value))}
              className="w-full accent-[#FB923C] bg-[#1A1D23] rounded h-1.5 mt-1 cursor-pointer"
            />
          </div>
        </div>

        {/* TinyML LSTM Fatigue Controls */}
        <div className="space-y-3 bg-[#0F1116] p-4 rounded-lg border border-[#1E2128]">
          <div className="flex items-center justify-between">
            <span className="font-bold text-[#78DCE8] flex items-center gap-1.5">
              <ShieldAlert className="w-3.5 h-3.5" />
              LSTM Fatigue Index
            </span>
            <span className="text-[#78DCE8] font-bold">{(params.fatigueIndex * 100).toFixed(0)}%</span>
          </div>
          <input
            type="range"
            min="0.0"
            max="1.0"
            step="0.05"
            value={params.fatigueIndex}
            onChange={(e) => {
              const fatigue = parseFloat(e.target.value);
              onParamChange('fatigueIndex', fatigue);
              onParamChange('iirAlpha', parseFloat((1.0 - fatigue * 0.4).toFixed(3)));
            }}
            className="w-full accent-[#78DCE8] bg-[#1A1D23] rounded h-1.5 cursor-pointer"
          />
          <div className="flex justify-between text-[10px] text-[#555]">
            <span>0% (Fresh)</span>
            <span>100% (Fatigued)</span>
          </div>

          <div className="pt-2 border-t border-[#1E2128] flex justify-between text-[11px]">
            <span className="text-[#888]">Dynamic IIR Alpha:</span>
            <span className="text-[#4ADE80] font-bold">{params.iirAlpha.toFixed(2)}</span>
          </div>
        </div>

        {/* 3D HRTF Crosstalk Controls */}
        <div className="space-y-3 bg-[#0F1116] p-4 rounded-lg border border-[#1E2128]">
          <div className="flex items-center justify-between">
            <span className="font-bold text-[#3B82F6] flex items-center gap-1.5">
              <Cpu className="w-3.5 h-3.5" />
              HRTF Crosstalk
            </span>
            <span className="text-[#3B82F6] font-bold">{(params.crosstalkGain * 100).toFixed(0)}%</span>
          </div>
          <input
            type="range"
            min="0.0"
            max="0.6"
            step="0.02"
            value={params.crosstalkGain}
            onChange={(e) => onParamChange('crosstalkGain', parseFloat(e.target.value))}
            className="w-full accent-[#3B82F6] bg-[#1A1D23] rounded h-1.5 cursor-pointer"
          />
          <div className="flex justify-between text-[10px] text-[#555]">
            <span>0% (Stereo Isolated)</span>
            <span>60% (3D Spatial Stage)</span>
          </div>

          <div className="pt-2 border-t border-[#1E2128] flex justify-between text-[11px]">
            <span className="text-[#888]">ITD Delay:</span>
            <span className="text-white font-bold">{params.hrtfDelayMs.toFixed(2)} ms</span>
          </div>
        </div>

        {/* LM-CMA-ES Evolution Rate Controls */}
        <div className="space-y-3 bg-[#0F1116] p-4 rounded-lg border border-[#1E2128]">
          <div className="flex items-center justify-between">
            <span className="font-bold text-[#4ADE80] flex items-center gap-1.5">
              <Waves className="w-3.5 h-3.5" />
              CMA-ES Step Size
            </span>
            <span className="text-[#4ADE80] font-bold">{params.eqMutationRate.toFixed(2)}</span>
          </div>
          <input
            type="range"
            min="0.02"
            max="0.30"
            step="0.01"
            value={params.eqMutationRate}
            onChange={(e) => onParamChange('eqMutationRate', parseFloat(e.target.value))}
            className="w-full accent-[#4ADE80] bg-[#1A1D23] rounded h-1.5 cursor-pointer"
          />
          <div className="flex justify-between text-[10px] text-[#555]">
            <span>0.02 (Precision)</span>
            <span>0.30 (Aggressive)</span>
          </div>

          <div className="pt-2 border-t border-[#1E2128] flex justify-between text-[11px]">
            <span className="text-[#888]">FIR Taps / Bands:</span>
            <span className="text-[#4ADE80] font-bold">256 / 512</span>
          </div>
        </div>

      </div>
    </div>
  );
};

