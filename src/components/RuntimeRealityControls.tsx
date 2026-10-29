import { useState } from 'react';
import { applyRuntimeControl } from '../lib/runtimeControl';

/** Commands are explicit: opening a visualization never changes the audio engine. */
export function RuntimeRealityControls() {
  const [intensity, setIntensity] = useState(0.5);
  const [enabled, setEnabled] = useState(false);
  const [status, setStatus] = useState('Sin comandos enviados. Los valores son una propuesta local.');
  const apply = (name: string, value: number) => {
    const native = window.IvannaFotoHoloBridge;
    const bridge = native?.applyRuntimeControl ? {
      applyRuntimeControl: (key: string, next: number) => native.applyRuntimeControl!(key, next),
    } : undefined;
    setStatus(`${new Date().toLocaleTimeString()} · ${applyRuntimeControl(bridge, name, value).message}`);
  };
  return <section className="rounded-xl border border-cyan-400/40 bg-slate-950 p-4 text-slate-200 space-y-3">
    <h2 className="font-semibold">Acoustic Reality · Control nativo</h2>
    <p className="text-sm">Comandos explícitos al motor Android. La lectura de retorno confirma el parámetro;
      la mejora acústica requiere medición en la salida activa.</p>
    <label className="flex items-center gap-2"><input type="checkbox" checked={enabled}
      onChange={e => { setEnabled(e.target.checked); setStatus('Propuesta modificada; aún no enviada.'); }} /> Activación solicitada</label>
    <button className="rounded border border-slate-500 px-3 py-2" onClick={() => apply('realityEnabled', enabled ? 1 : 0)}>
      Enviar activación</button>
    <label className="block">Intensidad solicitada: {Math.round(intensity * 100)}%
      <input className="block w-full accent-cyan-400" type="range" min={0} max={1} step={0.01} value={intensity}
        onChange={e => { setIntensity(Number(e.target.value)); setStatus('Propuesta modificada; aún no enviada.'); }} />
    </label>
    <button className="rounded border border-slate-500 px-3 py-2" onClick={() => apply('realityIntensity', intensity)}>
      Enviar intensidad</button>
    <p role="status" className="text-sm">Última operación: {status}</p>
  </section>;
}
