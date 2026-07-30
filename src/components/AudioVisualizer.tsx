import React, { useEffect, useRef, useState } from 'react';
import { DspParameters } from '../types';
import { Play, Pause, Radio, Activity } from 'lucide-react';

interface AudioVisualizerProps {
  params: DspParameters;
}

export const AudioVisualizer: React.FC<AudioVisualizerProps> = ({ params }) => {
  const oscCanvasRef = useRef<HTMLCanvasElement | null>(null);
  const specCanvasRef = useRef<HTMLCanvasElement | null>(null);

  const [isPlaying, setIsPlaying] = useState(false);
  const [signalType, setSignalType] = useState<'sine' | 'impulse' | 'harmonics' | 'noise'>('harmonics');
  const [fundamentalFreq, setFundamentalFreq] = useState(440);

  // Web Audio Context & Oscillator setup for audio preview
  const audioCtxRef = useRef<AudioContext | null>(null);
  const oscNodeRef = useRef<OscillatorNode | null>(null);
  const gainNodeRef = useRef<GainNode | null>(null);

  const toggleAudio = () => {
    if (isPlaying) {
      if (audioCtxRef.current) {
        audioCtxRef.current.suspend();
      }
      setIsPlaying(false);
    } else {
      if (!audioCtxRef.current) {
        const AudioCtx = window.AudioContext || (window as unknown as { webkitAudioContext: typeof AudioContext }).webkitAudioContext;
        const ctx = new AudioCtx();
        const gain = ctx.createGain();
        gain.gain.value = 0.15;
        gain.connect(ctx.destination);

        const osc = ctx.createOscillator();
        osc.type = signalType === 'sine' ? 'sine' : signalType === 'noise' ? 'sawtooth' : 'triangle';
        osc.frequency.setValueAtTime(fundamentalFreq, ctx.currentTime);
        osc.connect(gain);
        osc.start();

        audioCtxRef.current = ctx;
        oscNodeRef.current = osc;
        gainNodeRef.current = gain;
      } else {
        audioCtxRef.current.resume();
      }
      setIsPlaying(true);
    }
  };

  useEffect(() => {
    if (oscNodeRef.current && audioCtxRef.current) {
      oscNodeRef.current.frequency.setValueAtTime(fundamentalFreq, audioCtxRef.current.currentTime);
    }
  }, [fundamentalFreq]);

  // Canvas Oscilloscope & Spectrum Simulation Loop
  useEffect(() => {
    let animId: number;
    let phase = 0;

    const render = () => {
      phase += 0.08;

      // 1. Oscilloscope Render
      const oscCanvas = oscCanvasRef.current;
      if (oscCanvas) {
        const ctx = oscCanvas.getContext('2d');
        if (ctx) {
          const w = oscCanvas.width;
          const h = oscCanvas.height;

          ctx.fillStyle = '#0F1116';
          ctx.fillRect(0, 0, w, h);

          // Grid lines
          ctx.strokeStyle = '#1E2128';
          ctx.lineWidth = 1;
          ctx.beginPath();
          ctx.moveTo(0, h / 2);
          ctx.lineTo(w, h / 2);
          ctx.stroke();

          // Raw Input Wave (Dimmed)
          ctx.strokeStyle = 'rgba(136, 136, 136, 0.4)';
          ctx.lineWidth = 1.5;
          ctx.beginPath();
          for (let x = 0; x < w; x++) {
            const t = (x / w) * Math.PI * 8 + phase;
            let val = 0;
            if (signalType === 'sine') val = Math.sin(t);
            else if (signalType === 'harmonics') val = Math.sin(t) + 0.3 * Math.sin(2 * t) + 0.2 * Math.sin(3 * t);
            else if (signalType === 'impulse') val = (x % 60 === 0) ? 0.9 : 0;
            else val = (Math.random() - 0.5) * 1.5;

            const y = h / 2 - val * (h * 0.3);
            if (x === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
          }
          ctx.stroke();

          // Processed Output Wave (IVANNA DSP Kernel)
          ctx.strokeStyle = params.goldenEarEnabled ? '#FB923C' : '#4ADE80';
          ctx.lineWidth = 2.5;
          ctx.shadowBlur = 8;
          ctx.shadowColor = params.goldenEarEnabled ? '#FB923C' : '#4ADE80';
          ctx.beginPath();

          for (let x = 0; x < w; x++) {
            const t = (x / w) * Math.PI * 8 + phase;
            let raw = 0;
            if (signalType === 'sine') raw = Math.sin(t);
            else if (signalType === 'harmonics') raw = Math.sin(t) + 0.3 * Math.sin(2 * t) + 0.2 * Math.sin(3 * t);
            else if (signalType === 'impulse') raw = (x % 60 === 0) ? 0.9 : 0;
            else raw = (Math.random() - 0.5) * 1.5;

            // Apply fatigue damping
            let out = raw * params.iirAlpha;

            // Apply Golden Ear Chebyshev harmonics
            if (params.goldenEarEnabled) {
              const drv = out * params.goldenEarDrive;
              const h2 = 2.0 * (drv * drv) - 1.0;
              const h3 = 4.0 * (drv * drv * drv) - 3.0 * drv;
              out = out + (h2 * 0.12 + h3 * 0.08) * params.goldenEarMix;
              // Soft tanh clip
              out = Math.tanh(out);
            }

            const y = h / 2 - out * (h * 0.32);
            if (x === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
          }
          ctx.stroke();
          ctx.shadowBlur = 0;
        }
      }

      // 2. Spectrum Analyzer Render
      const specCanvas = specCanvasRef.current;
      if (specCanvas) {
        const ctx = specCanvas.getContext('2d');
        if (ctx) {
          const w = specCanvas.width;
          const h = specCanvas.height;

          ctx.fillStyle = '#0F1116';
          ctx.fillRect(0, 0, w, h);

          const bars = 64;
          const barWidth = w / bars;

          for (let i = 0; i < bars; i++) {
            const freqRatio = i / bars;
            let magnitude = Math.exp(-freqRatio * 3) * (0.6 + 0.4 * Math.sin(phase * 2 + i * 0.3));

            // High frequency roll-off due to fatigue IIR
            if (i > 30) {
              magnitude *= params.iirAlpha;
            }

            // Harmonic peaks if Golden Ear is active
            if (params.goldenEarEnabled && (i % 8 === 0 || i % 12 === 0)) {
              magnitude += 0.25 * params.goldenEarMix;
            }

            const barHeight = Math.min(h * 0.85, magnitude * h);

            const gradient = ctx.createLinearGradient(0, h, 0, h - barHeight);
            if (params.goldenEarEnabled) {
              gradient.addColorStop(0, '#78350F');
              gradient.addColorStop(0.5, '#FB923C');
              gradient.addColorStop(1, '#FEF08A');
            } else {
              gradient.addColorStop(0, '#166534');
              gradient.addColorStop(0.5, '#4ADE80');
              gradient.addColorStop(1, '#BBF7D0');
            }

            ctx.fillStyle = gradient;
            ctx.fillRect(i * barWidth, h - barHeight, barWidth - 2, barHeight);
          }
        }
      }

      animId = requestAnimationFrame(render);
    };

    render();

    return () => {
      cancelAnimationFrame(animId);
    };
  }, [params, signalType]);

  return (
    <div className="space-y-6 font-mono text-xs">
      
      {/* Controls Bar */}
      <div className="bg-[#12141A] border border-[#2A2D35] rounded-xl p-4 flex flex-col md:flex-row md:items-center justify-between gap-4">
        
        {/* Signal Selector */}
        <div className="flex items-center space-x-2 overflow-x-auto text-xs">
          <span className="text-[#888] font-bold mr-2 uppercase text-[11px]">Signal Input:</span>
          {(['harmonics', 'sine', 'impulse', 'noise'] as const).map((type) => (
            <button
              key={type}
              onClick={() => setSignalType(type)}
              className={`px-3 py-1.5 rounded border uppercase transition-all text-xs ${
                signalType === type
                  ? 'bg-[#1A1D23] border-[#4ADE80] text-[#4ADE80] font-bold'
                  : 'bg-[#0F1116] border-[#1E2128] text-[#888] hover:text-white'
              }`}
            >
              {type}
            </button>
          ))}
        </div>

        {/* Fundamental Freq Slider & Audio Toggle */}
        <div className="flex items-center space-x-4 text-xs">
          <div className="flex items-center space-x-2">
            <span className="text-[#888]">Fundamental:</span>
            <input
              type="range"
              min="100"
              max="2000"
              step="10"
              value={fundamentalFreq}
              onChange={(e) => setFundamentalFreq(Number(e.target.value))}
              className="w-28 accent-[#4ADE80] bg-[#1A1D23] rounded h-1.5 cursor-pointer"
            />
            <span className="text-[#4ADE80] font-bold w-12">{fundamentalFreq}Hz</span>
          </div>

          <button
            onClick={toggleAudio}
            className={`flex items-center space-x-2 px-3.5 py-1.5 rounded text-xs font-bold transition-all border ${
              isPlaying
                ? 'bg-[#1E2229] border-[#FF6188] text-[#FF6188]'
                : 'bg-[#1E2229] border-[#4ADE80] text-[#4ADE80] hover:bg-[#2A2F3A]'
            }`}
          >
            {isPlaying ? <Pause className="w-3.5 h-3.5" /> : <Play className="w-3.5 h-3.5" />}
            <span>{isPlaying ? 'MUTE AUDIO' : 'PLAY SENSE'}</span>
          </button>
        </div>

      </div>

      {/* Visualizers Grid */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-4">
        
        {/* Oscilloscope */}
        <div className="bg-[#12141A] border border-[#2A2D35] rounded-xl p-4 space-y-3">
          <div className="flex items-center justify-between text-xs">
            <div className="flex items-center space-x-2 text-white font-bold">
              <Activity className="w-4 h-4 text-[#4ADE80]" />
              <span>1024-Sample Oscilloscope</span>
            </div>
            <div className="flex items-center space-x-3 text-[11px]">
              <span className="flex items-center gap-1 text-[#888]">
                <span className="w-2.5 h-0.5 bg-[#555] inline-block"></span> Raw Input
              </span>
              <span className="flex items-center gap-1 text-[#4ADE80] font-bold">
                <span className={`w-2.5 h-0.5 inline-block ${params.goldenEarEnabled ? 'bg-[#FB923C]' : 'bg-[#4ADE80]'}`}></span> IVANNA DSP
              </span>
            </div>
          </div>
          <div className="relative rounded overflow-hidden border border-[#1E2128] bg-[#0F1116]">
            <canvas
              ref={oscCanvasRef}
              width={600}
              height={260}
              className="w-full h-56 object-cover"
            />
          </div>
        </div>

        {/* Spectrum Analyzer */}
        <div className="bg-[#12141A] border border-[#2A2D35] rounded-xl p-4 space-y-3">
          <div className="flex items-center justify-between text-xs">
            <div className="flex items-center space-x-2 text-white font-bold">
              <Radio className="w-4 h-4 text-[#4ADE80]" />
              <span>Real-time 64-Band FFT Spectrum</span>
            </div>
            <div className="text-xs text-[#888]">
              0 Hz — 24,000 Hz
            </div>
          </div>
          <div className="relative rounded overflow-hidden border border-[#1E2128] bg-[#0F1116]">
            <canvas
              ref={specCanvasRef}
              width={600}
              height={260}
              className="w-full h-56 object-cover"
            />
          </div>
        </div>

      </div>

    </div>
  );
};

