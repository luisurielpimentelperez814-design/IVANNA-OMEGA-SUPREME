import { useCallback, useEffect, useRef, useState } from 'react';
import { Download, RotateCcw, Sparkles } from 'lucide-react';

type Preset = 'nanotomix' | 'concert' | 'studio' | 'orbital';
const presets: Record<Preset, { label: string; spread: number; depth: number; lift: number }> = {
  nanotomix: { label: 'FotoHolo Nanotomix', spread: 1.25, depth: 1.1, lift: 0.55 },
  concert: { label: 'Concierto inmersivo', spread: 1.65, depth: 1.45, lift: 0.8 },
  studio: { label: 'Estudio de precisión', spread: 0.82, depth: 0.72, lift: 0.28 },
  orbital: { label: 'Campo orbital', spread: 1.4, depth: 1.8, lift: 1.05 },
};
const points = Array.from({ length: 460 }, (_, i) => {
  const a = i * 2.399963, y = 1 - 2 * (i + 0.5) / 460, r = Math.sqrt(1 - y * y);
  return { x: Math.cos(a) * r, y, z: Math.sin(a) * r, hue: 178 + (i * 37 % 105), size: 0.7 + (i * 13 % 18) / 10 };
});

export function FotoHolo3DGenerator() {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const frameRef = useRef<number | null>(null);
  const [preset, setPreset] = useState<Preset>('nanotomix');
  const [width, setWidth] = useState(72);
  const [depth, setDepth] = useState(58);
  const [elevation, setElevation] = useState(36);
  const [rotation, setRotation] = useState(18);
  const [motion, setMotion] = useState(true);
  const [guides, setGuides] = useState(true);

  const draw = useCallback((time: number) => {
    const canvas = canvasRef.current, ctx = canvas?.getContext('2d');
    if (!canvas || !ctx) return;
    const rect = canvas.getBoundingClientRect(), dpr = Math.min(window.devicePixelRatio || 1, 2);
    const w = Math.max(1, Math.floor(rect.width * dpr)), h = Math.max(1, Math.floor(rect.height * dpr));
    if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0); ctx.clearRect(0, 0, rect.width, rect.height);
    const bg = ctx.createRadialGradient(rect.width / 2, rect.height / 2, 2, rect.width / 2, rect.height / 2, Math.max(rect.width, rect.height) * .7);
    bg.addColorStop(0, '#11263c'); bg.addColorStop(1, '#060a12'); ctx.fillStyle = bg; ctx.fillRect(0, 0, rect.width, rect.height);
    const p = presets[preset], angle = (rotation + (motion ? time * .008 : 0)) * Math.PI / 180;
    const tilt = elevation / 100 * Math.PI * .8, scale = Math.min(rect.width, rect.height) * .32;
    const projected = points.map(q => {
      const x = q.x * p.spread * width / 72, y = q.y * p.lift * (elevation / 36 || .2), z = q.z * p.depth * depth / 58;
      const rx = x * Math.cos(angle) - z * Math.sin(angle), rz = x * Math.sin(angle) + z * Math.cos(angle);
      const ry = y * Math.cos(tilt) - rz * Math.sin(tilt), zz = y * Math.sin(tilt) + rz * Math.cos(tilt), persp = 3.2 / (3.2 + zz);
      return { x: rect.width / 2 + rx * scale * persp, y: rect.height / 2 + ry * scale * persp, z: zz, persp, hue: q.hue, size: q.size };
    }).sort((a, b) => a.z - b.z);
    if (guides) { ctx.save(); ctx.translate(rect.width / 2, rect.height / 2); ctx.strokeStyle = 'rgba(87,190,255,.16)';
      [0.45, 0.72, 1].forEach(f => { ctx.beginPath(); ctx.ellipse(0, 0, scale * f * width / 72, scale * f * .42, -angle * .35, 0, Math.PI * 2); ctx.stroke(); });
      ctx.beginPath(); ctx.moveTo(-rect.width * .43, 0); ctx.lineTo(rect.width * .43, 0); ctx.moveTo(0, -rect.height * .43); ctx.lineTo(0, rect.height * .43); ctx.stroke(); ctx.restore(); }
    projected.forEach(q => { ctx.beginPath(); ctx.fillStyle = 'hsla(' + q.hue + ',94%,68%,' + Math.max(.16, Math.min(.94, .75 - q.z * .12)) + ')'; ctx.shadowColor = 'hsla(' + q.hue + ',100%,64%,.7)'; ctx.shadowBlur = q.size * 3; ctx.arc(q.x, q.y, q.size * q.persp, 0, Math.PI * 2); ctx.fill(); });
    ctx.shadowBlur = 0; ctx.fillStyle = 'rgba(224,244,255,.86)'; ctx.font = '11px ui-monospace,monospace'; ctx.fillText('FOTOHOLO · 3D FIELD', 16, 22); ctx.fillStyle = 'rgba(125,211,252,.66)'; ctx.fillText('460 PARTICLES / PROCEDURAL PREVIEW', 16, rect.height - 16);
    if (motion) frameRef.current = requestAnimationFrame(draw);
  }, [preset, width, depth, elevation, rotation, motion, guides]);
  useEffect(() => { frameRef.current = requestAnimationFrame(draw); return () => { if (frameRef.current !== null) cancelAnimationFrame(frameRef.current); }; }, [draw]);
  const exportPng = () => { const canvas = canvasRef.current; if (!canvas) return; const a = document.createElement('a'); a.download = 'ivanna-fotoholo-3d-' + preset + '.png'; a.href = canvas.toDataURL('image/png'); a.click(); };
  const reset = () => { setPreset('nanotomix'); setWidth(72); setDepth(58); setElevation(36); setRotation(18); setMotion(true); setGuides(true); };
  const slider = (label: string, value: number, setter: (n: number) => void, max: number, low: string, high: string) => <label key={label} className="block text-xs text-slate-300">{label}<span className="float-right font-mono text-cyan-300">{value}{label === 'Ángulo base' ? '°' : '%'}</span><input type="range" min="0" max={max} value={value} onChange={e => setter(Number(e.target.value))} className="mt-2 w-full accent-cyan-300"/><span className="flex justify-between text-[10px] text-slate-500"><span>{low}</span><span>{high}</span></span></label>;
  return <section className="space-y-5 rounded-2xl border border-cyan-400/20 bg-[#0c111b] p-4 shadow-[0_0_42px_rgba(34,211,238,.06)] sm:p-6">
    <div className="flex flex-wrap items-start justify-between gap-3"><div><div className="flex items-center gap-2 text-xs font-mono tracking-[.2em] text-cyan-300"><Sparkles size={15}/> GENERADOR VISUAL 3D</div><h2 className="mt-2 text-xl font-semibold text-slate-100 sm:text-2xl">FotoHolo Nanotomix · Campo espacial</h2><p className="mt-1 max-w-2xl text-sm text-slate-400">Crea una vista 3D interactiva del campo espacial y exporta la imagen PNG.</p></div><div className="flex gap-2"><button onClick={reset} className="inline-flex items-center gap-2 rounded-lg border border-slate-700 px-3 py-2 text-xs text-slate-200"><RotateCcw size={14}/> Restablecer</button><button onClick={exportPng} className="inline-flex items-center gap-2 rounded-lg bg-cyan-300 px-3 py-2 text-xs font-semibold text-slate-950"><Download size={14}/> Exportar PNG</button></div></div>
    <div className="grid grid-cols-1 gap-4 lg:grid-cols-[minmax(0,1.6fr)_minmax(250px,.8fr)]"><div className="overflow-hidden rounded-xl border border-slate-700/70"><canvas ref={canvasRef} aria-label="Visualización 3D interactiva del campo FotoHolo" className="block h-[320px] w-full sm:h-[400px]"/><div className="flex justify-between border-t border-slate-800 bg-slate-950/70 px-3 py-2 text-[10px] font-mono text-slate-400"><span>VISTA PROCEDURAL · 460 PUNTOS</span><span className="text-cyan-300">{motion ? 'ROTACIÓN ACTIVA' : 'PAUSADA'}</span></div></div>
    <div className="space-y-4 rounded-xl border border-slate-800 bg-slate-950/50 p-4"><label className="block text-xs text-slate-300">Escena / preset<select value={preset} onChange={e => setPreset(e.target.value as Preset)} className="mt-2 w-full rounded-lg border border-slate-700 bg-slate-900 p-2.5 text-sm text-slate-100">{Object.entries(presets).map(([k, v]) => <option key={k} value={k}>{v.label}</option>)}</select></label>{slider('Anchura espacial', width, setWidth, 100, 'Estrecha', 'Amplia')}{slider('Profundidad', depth, setDepth, 100, 'Cercana', 'Profunda')}{slider('Elevación', elevation, setElevation, 100, 'Horizontal', 'Elevada')}{slider('Ángulo base', rotation, setRotation, 360, '0°', '360°')}<div className="flex flex-wrap gap-4 border-t border-slate-800 pt-3"><label className="flex items-center gap-2 text-xs text-slate-300"><input type="checkbox" checked={motion} onChange={e => setMotion(e.target.checked)} className="accent-cyan-300"/> Animación</label><label className="flex items-center gap-2 text-xs text-slate-300"><input type="checkbox" checked={guides} onChange={e => setGuides(e.target.checked)} className="accent-cyan-300"/> Guías 3D</label></div><p className="rounded-lg border border-amber-300/20 bg-amber-300/5 p-3 text-[11px] leading-relaxed text-amber-100/80">Escena procedural visual. No representa medición acústica ni telemetría DSP real; esa conexión debe validarse por separado.</p></div></div>
  </section>;
}
