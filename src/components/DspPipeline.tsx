import React from 'react';
import { DspParameters } from '../types';
import { Cpu, Zap, Activity, Waves, ShieldAlert, Layers, Headphones, Radio, ShieldCheck, Compass } from 'lucide-react';

interface DspPipelineProps {
  params: DspParameters;
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
              IVANNA-OMEGA-SUPREME — Arquitectura Integral de 7 Ejes Acústicos
            </h2>
            <p className="text-xs text-[#888] font-mono mt-1 max-w-3xl">
              100% ARMv8 NEON SIMD (<code className="text-[#4ADE80]">float32x4_t</code>, <code className="text-[#4ADE80]">vfmaq_f32</code>). Zero heap allocations en el hot-path del kernel audio thread, latencia algorítmica cero añadida, garantía estricta de tiempo real RT-Safety y mitigación acústica anti-Dolby.
            </p>
          </div>
          <div className="flex items-center gap-3 font-mono text-xs">
            <div className="px-3 py-1.5 bg-[#0F1116] border border-[#1E2128] rounded">
              <span className="text-[#555] block text-[9px] font-bold uppercase">SAMPLE RATE</span>
              <span className="text-white font-bold">{params.sampleRate.toLocaleString()} Hz</span>
            </div>
            <div className="px-3 py-1.5 bg-[#0F1116] border border-[#1E2128] rounded">
              <span className="text-[#555] block text-[9px] font-bold uppercase">CADENCIA / LATENCIA</span>
              <span className="text-[#4ADE80] font-bold">{params.blockSize} fr / 0 ms added</span>
            </div>
            <div className="px-3 py-1.5 bg-[#0F1116] border border-[#1E2128] rounded">
              <span className="text-[#555] block text-[9px] font-bold uppercase">ALINEACIÓN L1 CACHE</span>
              <span className="text-[#FB923C] font-bold">alignas(16) / 64B</span>
            </div>
          </div>
        </div>
      </div>

      {/* 7-Axis Spatial Pipeline Grid */}
      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 xl:grid-cols-7 gap-3">
        
        {/* EJE 1: StereoObjectDecomposer */}
        <div className="bg-[#12141A] border border-[#2A2D35] rounded-xl p-3.5 space-y-2.5 relative hover:border-[#38BDF8] transition-colors">
          <div className="flex items-center justify-between">
            <span className="text-[9px] font-mono font-bold px-1.5 py-0.5 rounded bg-[#1A1D23] text-[#38BDF8] border border-[#2A2D35]">
              EJE 1
            </span>
            <Layers className="w-3.5 h-3.5 text-[#38BDF8]" />
          </div>
          <h3 className="text-xs font-bold font-mono text-white">
            Stereo Object Decomposer
          </h3>
          <p className="text-[10px] text-[#888] font-mono leading-relaxed">
            Separación WFS Mid/Side con filtros de 1 polo en 4 objetos discretos: Center, Left, Right y Ambient.
          </p>
          <div className="pt-2 border-t border-[#1E2128] font-mono text-[10px] space-y-1">
            <div className="flex justify-between text-[#888]">
              <span>Objetos WFS:</span>
              <span className="text-[#38BDF8] font-bold">4 Discretos</span>
            </div>
            <div className="flex justify-between text-[#888]">
              <span>M/S Filtros:</span>
              <span className="text-white">1-Pole IIR</span>
            </div>
            <div className="flex justify-between text-[#888]">
              <span>Latencia:</span>
              <span className="text-[#4ADE80] font-bold">0 frames</span>
            </div>
          </div>
        </div>

        {/* EJE 2: HrtfPersonalizer */}
        <div className="bg-[#12141A] border border-[#2A2D35] rounded-xl p-3.5 space-y-2.5 relative hover:border-[#A855F7] transition-colors">
          <div className="flex items-center justify-between">
            <span className="text-[9px] font-mono font-bold px-1.5 py-0.5 rounded bg-[#1A1D23] text-[#A855F7] border border-[#2A2D35]">
              EJE 2
            </span>
            <Headphones className="w-3.5 h-3.5 text-[#A855F7]" />
          </div>
          <h3 className="text-xs font-bold font-mono text-white">
            HRTF Personalizer
          </h3>
          <p className="text-[10px] text-[#888] font-mono leading-relaxed">
            Personalización anatómica de pinna y canal auditivo. Interpolación bilineal IDW continua sin pops.
          </p>
          <div className="pt-2 border-t border-[#1E2128] font-mono text-[10px] space-y-1">
            <div className="flex justify-between text-[#888]">
              <span>Pinna Notches:</span>
              <span className="text-[#A855F7] font-bold">6k - 9kHz</span>
            </div>
            <div className="flex justify-between text-[#888]">
              <span>Interpolación:</span>
              <span className="text-white">IDW Bilinear</span>
            </div>
            <div className="flex justify-between text-[#888]">
              <span>ITD Head Radius:</span>
              <span className="text-[#A855F7] font-bold">8.75 cm</span>
            </div>
          </div>
        </div>

        {/* EJE 3: RoomProjectionEngine */}
        <div className="bg-[#12141A] border border-[#2A2D35] rounded-xl p-3.5 space-y-2.5 relative hover:border-[#34D399] transition-colors">
          <div className="flex items-center justify-between">
            <span className="text-[9px] font-mono font-bold px-1.5 py-0.5 rounded bg-[#1A1D23] text-[#34D399] border border-[#2A2D35]">
              EJE 3
            </span>
            <Radio className="w-3.5 h-3.5 text-[#34D399]" />
          </div>
          <h3 className="text-xs font-bold font-mono text-white">
            Room Projection & Inversion
          </h3>
          <p className="text-[10px] text-[#888] font-mono leading-relaxed">
            Inversión parcial de sala RIR con particiones overlap-save y síntesis de reflexiones tempranas.
          </p>
          <div className="pt-2 border-t border-[#1E2128] font-mono text-[10px] space-y-1">
            <div className="flex justify-between text-[#888]">
              <span>Convolución:</span>
              <span className="text-[#34D399] font-bold">Overlap-Save</span>
            </div>
            <div className="flex justify-between text-[#888]">
              <span>Reflexiones:</span>
              <span className="text-white">Image-Source</span>
            </div>
            <div className="flex justify-between text-[#888]">
              <span>Inversión RIR:</span>
              <span className="text-[#34D399] font-bold">Minimum-Phase</span>
            </div>
          </div>
        </div>

        {/* EJE 4: ObjectSpatialRenderer */}
        <div className="bg-[#12141A] border border-[#2A2D35] rounded-xl p-3.5 space-y-2.5 relative hover:border-[#F59E0B] transition-colors">
          <div className="flex items-center justify-between">
            <span className="text-[9px] font-mono font-bold px-1.5 py-0.5 rounded bg-[#1A1D23] text-[#F59E0B] border border-[#2A2D35]">
              EJE 4
            </span>
            <Compass className="w-3.5 h-3.5 text-[#F59E0B]" />
          </div>
          <h3 className="text-xs font-bold font-mono text-white">
            Object Spatial Renderer
          </h3>
          <p className="text-[10px] text-[#888] font-mono leading-relaxed">
            Posicionamiento 3D tridimensional de objetos con coordenadas esféricas (azimut, elevación, distancia).
          </p>
          <div className="pt-2 border-t border-[#1E2128] font-mono text-[10px] space-y-1">
            <div className="flex justify-between text-[#888]">
              <span>Ángulo Azimut:</span>
              <span className="text-[#F59E0B] font-bold">{params.spatialAngleDeg}°</span>
            </div>
            <div className="flex justify-between text-[#888]">
              <span>Soundstage:</span>
              <span className="text-white">{(params.spatialWidth * 100).toFixed(0)}%</span>
            </div>
            <div className="flex justify-between text-[#888]">
              <span>Doppler:</span>
              <span className="text-[#4ADE80] font-bold">Sub-sample</span>
            </div>
          </div>
        </div>

        {/* EJE 5: PhysicalSceneRenderer */}
        <div className="bg-[#12141A] border border-[#2A2D35] rounded-xl p-3.5 space-y-2.5 relative hover:border-[#EC4899] transition-colors">
          <div className="flex items-center justify-between">
            <span className="text-[9px] font-mono font-bold px-1.5 py-0.5 rounded bg-[#1A1D23] text-[#EC4899] border border-[#2A2D35]">
              EJE 5
            </span>
            <Waves className="w-3.5 h-3.5 text-[#EC4899]" />
          </div>
          <h3 className="text-xs font-bold font-mono text-white">
            Physical Scene Renderer
          </h3>
          <p className="text-[10px] text-[#888] font-mono leading-relaxed">
            Oclusión acústica, refracción en bordes y coeficientes de transmisión dependientes de material.
          </p>
          <div className="pt-2 border-t border-[#1E2128] font-mono text-[10px] space-y-1">
            <div className="flex justify-between text-[#888]">
              <span>Oclusión:</span>
              <span className="text-[#EC4899] font-bold">Biquad Low-Pass</span>
            </div>
            <div className="flex justify-between text-[#888]">
              <span>Material Abs:</span>
              <span className="text-white">Sabine / Eyring</span>
            </div>
            <div className="flex justify-between text-[#888]">
              <span>Difracción:</span>
              <span className="text-[#EC4899] font-bold">BTM Biot-Tolstoy</span>
            </div>
          </div>
        </div>

        {/* EJE 6: HearingAdaptationEngine */}
        <div className="bg-[#12141A] border border-[#2A2D35] rounded-xl p-3.5 space-y-2.5 relative hover:border-[#10B981] transition-colors">
          <div className="flex items-center justify-between">
            <span className="text-[9px] font-mono font-bold px-1.5 py-0.5 rounded bg-[#1A1D23] text-[#10B981] border border-[#2A2D35]">
              EJE 6
            </span>
            <ShieldAlert className="w-3.5 h-3.5 text-[#10B981]" />
          </div>
          <h3 className="text-xs font-bold font-mono text-white">
            Hearing Adaptation
          </h3>
          <p className="text-[10px] text-[#888] font-mono leading-relaxed">
            Curvas ISO 226 dinámicas según SPL instantáneo y protección adaptativa contra fatiga coclear.
          </p>
          <div className="pt-2 border-t border-[#1E2128] font-mono text-[10px] space-y-1">
            <div className="flex justify-between text-[#888]">
              <span>Índice Fatiga:</span>
              <span className="text-[#10B981] font-bold">{(params.fatigueIndex * 100).toFixed(0)}%</span>
            </div>
            <div className="flex justify-between text-[#888]">
              <span>ISO 226 Phon:</span>
              <span className="text-white">65 Phon Ref</span>
            </div>
            <div className="flex justify-between text-[#888]">
              <span>Respuesta:</span>
              <span className="text-[#4ADE80] font-bold">Dinámica</span>
            </div>
          </div>
        </div>

        {/* EJE 7: PerfAuditor & Pipeline Integration */}
        <div className="bg-[#12141A] border border-[#2A2D35] rounded-xl p-3.5 space-y-2.5 relative hover:border-[#4ADE80] transition-colors">
          <div className="flex items-center justify-between">
            <span className="text-[9px] font-mono font-bold px-1.5 py-0.5 rounded bg-[#1A1D23] text-[#4ADE80] border border-[#2A2D35]">
              EJE 7
            </span>
            <ShieldCheck className="w-3.5 h-3.5 text-[#4ADE80]" />
          </div>
          <h3 className="text-xs font-bold font-mono text-white">
            Perf Auditor & RT-Safety
          </h3>
          <p className="text-[10px] text-[#888] font-mono leading-relaxed">
            Certificación en tiempo real de cero latencia inducida, cero locks y monitor de ciclo de reloj por muestra.
          </p>
          <div className="pt-2 border-t border-[#1E2128] font-mono text-[10px] space-y-1">
            <div className="flex justify-between text-[#888]">
              <span>Latencia Añadida:</span>
              <span className="text-[#4ADE80] font-bold">0.000 ms</span>
            </div>
            <div className="flex justify-between text-[#888]">
              <span>Heap Mallocs:</span>
              <span className="text-[#4ADE80] font-bold">0 en RT</span>
            </div>
            <div className="flex justify-between text-[#888]">
              <span>Host Suite:</span>
              <span className="text-[#4ADE80] font-bold">PASS 100%</span>
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
            Optimización SIMD ARM NEON & Coherencia de Fase
          </h3>
          <ul className="space-y-2 text-[#888] text-xs leading-relaxed">
            <li className="flex items-start gap-2">
              <span className="text-[#4ADE80] font-bold">1.</span>
              <span>
                <strong className="text-white">Fused Multiply-Add Vectorizado (FMA):</strong> Desentrelazado estéreo <code className="bg-[#0F1116] px-1 py-0.5 rounded border border-[#1E2128] text-[#4ADE80]">vld2q_f32</code> y acumulación de convolución con <code className="bg-[#0F1116] px-1 py-0.5 rounded border border-[#1E2128] text-[#4ADE80]">vfmaq_f32</code>, procesando 4 pares estéreo por ciclo.
              </span>
            </li>
            <li className="flex items-start gap-2">
              <span className="text-[#4ADE80] font-bold">2.</span>
              <span>
                <strong className="text-white">Aproximación Padé para Tanh:</strong> Evaluación analítica $f(x) = x(27 + x^2) / (27 + 9x^2)$ con estimación recíproca <code className="bg-[#0F1116] px-1 py-0.5 rounded text-[#FB923C]">vrecpeq_f32</code> y paso Newton-Raphson para soft-clipping transparente sin ramas condicionales.
              </span>
            </li>
            <li className="flex items-start gap-2">
              <span className="text-[#4ADE80] font-bold">3.</span>
              <span>
                <strong className="text-white">TinyML Neuromórfico en Reemplazo de YAMNet:</strong> Pi-LSTM de 32 dimensiones latentes sobre 64 Mel-bins con ring buffer SPSC wait-free; clasifica anomalías de compresión cada bloque en &lt; 0.2 ms en lugar de ventanas de 1000 ms.
              </span>
            </li>
          </ul>
        </div>

        {/* Zero-Allocation Rules */}
        <div className="bg-[#12141A] border border-[#2A2D35] rounded-xl p-5 space-y-3 font-mono text-xs">
          <h3 className="text-xs font-bold text-white flex items-center gap-2 tracking-wide uppercase">
            <Cpu className="w-4 h-4 text-[#4ADE80]" />
            Garantías RT-Safety & Gestión de Punteros
          </h3>
          <ul className="space-y-2 text-[#888] text-xs leading-relaxed">
            <li className="flex items-start gap-2">
              <span className="text-[#4ADE80] font-bold">1.</span>
              <span>
                <strong className="text-white">Audio Thread Libre de Heap:</strong> Cero <code className="text-[#FF6188]">malloc</code>, <code className="text-[#FF6188]">new</code> o redimensionamiento dinámico en el hot-path. Los scratch buffers son estáticos y aislados por canal en memoria preasignada.
              </span>
            </li>
            <li className="flex items-start gap-2">
              <span className="text-[#4ADE80] font-bold">2.</span>
              <span>
                <strong className="text-white">Alineación Estricta a Líneas de Caché L1/L2:</strong> Todos los buffers y ring buffers usan <code className="bg-[#0F1116] px-1 py-0.5 rounded text-[#78DCE8]">alignas(64)</code> para evitar interferencias por false sharing entre núcleos heterogéneos SMP (Cortex-X / A).
              </span>
            </li>
            <li className="flex items-start gap-2">
              <span className="text-[#4ADE80] font-bold">3.</span>
              <span>
                <strong className="text-white">Sincronización Lock-Free Atómica:</strong> Suavizado balístico asimétrico (Attack/Release) manejado con <code className="bg-[#0F1116] px-1 py-0.5 rounded text-[#4ADE80]">std::memory_order_relaxed</code> y publicación en bloque mediante <code className="bg-[#0F1116] px-1 py-0.5 rounded text-[#4ADE80]">memory_order_release</code>.
              </span>
            </li>
          </ul>
        </div>

      </div>

    </div>
  );
};

