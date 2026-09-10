import React from 'react';
import { usePersist } from '../usePersist';
import { Shield, Terminal, Database } from 'lucide-react';
import { motion, AnimatePresence } from 'motion/react';

export const MagiskIntegrationPanel: React.FC = () => {
  const [status, setStatus] = usePersist<'idle' | 'checking' | 'granted' | 'denied'>('magisk_status', 'idle');
  const [logs, setLogs] = usePersist<string[]>('magisk_logs', []);

  const addLog = (msg: string) => {
    setLogs(prev => [...prev, `[${new Date().toISOString().substring(11, 19)}] ${msg}`].slice(-6));
  };

  // FIX (auditoría 2026-09-10, flanco Dashboard web): este panel corre en un
  // navegador — NO hay forma técnica de que una página web ejecute `su -c id`
  // ni consulte el daemon real de un teléfono. El texto original ("Executing
  // su -c id probe...", "GRANTED") daba la impresión de una verificación
  // real cuando siempre era un timeout con éxito garantizado. Se deja
  // explícito como simulación ilustrativa del flujo que SÍ es real en la app
  // Android (MagiskStatusPanel.kt: botones ROOT PING/RECONECTAR) — el
  // dashboard web no tiene bridge a un dispositivo real, así que fingir lo
  // contrario es peor que no mostrar nada.
  const handleRootPing = () => {
    setStatus('checking');
    addLog('[SIMULACIÓN] su -c id (no es un dispositivo real)');
    addLog('[SIMULACIÓN] esperando respuesta del daemon…');

    setTimeout(() => {
      setStatus('granted');
      addLog('[SIMULACIÓN] permiso root GRANTED (ilustrativo)');
      addLog('[SIMULACIÓN] puente IPC del daemon establecido (ilustrativo)');
      addLog('[SIMULACIÓN] subsistema de audio enlazado (ilustrativo)');
    }, 1500);
  };

  const handleRestartDaemon = () => {
    addLog('[SIMULACIÓN] reiniciando Omega Audio Daemon…');
    setStatus('checking');
    setTimeout(() => {
      setStatus('granted');
      addLog('[SIMULACIÓN] daemon reiniciado. IPC reconectado (ilustrativo)');
    }, 1200);
  };

  return (
    <div className="bg-[#10131A] border border-[#232936] rounded-xl p-5 space-y-4 hover:border-[#EF4444]/50 transition-colors">
      <div className="flex items-center justify-between border-b border-[#1E2330] pb-2.5">
        <div className="flex items-center gap-2">
          <Shield className={`w-4 h-4 ${status === 'granted' ? 'text-green-500' : status === 'denied' ? 'text-red-500' : 'text-[#EF4444]'}`} />
          <h3 className="font-bold text-white uppercase text-xs">Kernel & Magisk IPC</h3>
          <span className="text-[9px] text-[#475569] italic">(simulación ilustrativa)</span>
        </div>
        <div className="flex items-center gap-2">
          {status === 'granted' && <span className="text-[10px] font-bold text-green-500 bg-green-500/10 px-2 py-0.5 rounded border border-green-500/20">HOOKED</span>}
          {status === 'denied' && <span className="text-[10px] font-bold text-red-500 bg-red-500/10 px-2 py-0.5 rounded border border-red-500/20">UNLINKED</span>}
        </div>
      </div>

      <div className="flex flex-col md:flex-row gap-4">
        <div className="flex-1 space-y-3">
          <p className="text-xs text-[#94A3B8] leading-relaxed">
            IVANNA-OMEGA-SUPREME requires root access via Magisk, KernelSU, or APatch to inject the C++ daemon into the Android audio surfaceflinger.
            Este panel web no tiene bridge a un dispositivo real — el botón simula
            el flujo de la app Android para referencia visual, no ejecuta nada.
          </p>
          <div className="flex gap-2">
            <button
              onClick={handleRootPing}
              disabled={status === 'checking'}
              className="flex-1 bg-[#2D1B14] hover:bg-[#3D251C] border border-[#F97316]/50 hover:border-[#F97316] text-[#F97316] text-xs font-bold py-2 px-3 rounded flex items-center justify-center gap-2 transition-all disabled:opacity-50"
            >
              <Terminal className="w-3.5 h-3.5" /> ROOT PING
            </button>
            <button
              onClick={handleRestartDaemon}
              disabled={status !== 'granted'}
              className="flex-1 bg-[#12151C] hover:bg-[#1A1D24] border border-[#1E2330] hover:border-[#334155] text-[#94A3B8] text-xs font-bold py-2 px-3 rounded flex items-center justify-center gap-2 transition-all disabled:opacity-50"
            >
              <Database className="w-3.5 h-3.5" /> RESTART DAEMON
            </button>
          </div>
        </div>

        <div className="flex-1 bg-[#0A0C10] border border-[#1E2330] rounded-lg p-3 relative overflow-hidden h-[100px] flex flex-col justify-end font-mono text-[10px]">
          <AnimatePresence>
            {logs.length === 0 && (
              <motion.div initial={{ opacity: 0 }} animate={{ opacity: 1 }} className="text-[#475569] italic">
                Awaiting kernel IPC commands...
              </motion.div>
            )}
            {logs.map((log, i) => (
              <motion.div
                key={i}
                initial={{ opacity: 0, x: -10 }}
                animate={{ opacity: 1, x: 0 }}
                className={`${log.includes('GRANTED') ? 'text-green-400' : log.includes('DENIED') ? 'text-red-400' : 'text-[#94A3B8]'}`}
              >
                <span className="text-[#38BDF8] mr-2">❯</span>{log}
              </motion.div>
            ))}
          </AnimatePresence>
        </div>
      </div>
    </div>
  );
};
