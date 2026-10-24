import { StrictMode, useEffect, useState } from 'react';
import './index.css';
import { createRoot } from 'react-dom/client';
import { FotoHolo3DGenerator } from './components/FotoHolo3DGenerator';
import type { DspParameters } from './types';

// hrtfEnabled arrancaba hardcodeado en `false` y esta vista embebida (la que
// realmente carga el WebView de Android en la pantalla FotoHolo) no tiene el
// panel SpatialHrtfPanel que existe en la app web completa — el usuario no
// tenía forma de cambiarlo ni de ver el estado real. Ahora arranca en `true`
// (igual que el default de App.tsx) y se re-sincroniza cada vez que llega
// telemetría nativa con el estado REAL del motor (hrtfRuntime), para que el
// valor nunca quede congelado mintiendo sobre lo que el motor está haciendo.
const initial: DspParameters = {
  masterGain: 1, antiDolbyIntensity: 0, masterBypass: false, goldenEarEnabled: false,
  goldenEarDrive: 1, goldenEarMix: 0, fatigueIndex: 0, iirAlpha: 0.9, crosstalkGain: 0,
  hrtfDelayMs: 0, spatialAngleDeg: 0, spatialWidth: 1, spatialWetEta: 0,
  eqMutationRate: 0, sampleRate: 48000, blockSize: 512, nhoAlpha: 0, nhoBeta: 0,
  harmonicGain: 1, hrtfEnabled: true, adaptEnabled: false, compThresholdDb: -18,
  compRatio: 2, compAttackMs: 10, compReleaseMs: 100, activePreset: 'custom',
};

function EmbeddedFotoHolo() {
  const [params, setParams] = useState<DspParameters>(initial);

  useEffect(() => {
    let alive = true;
    const sync = () => {
      if (!alive) return;
      try {
        const raw = window.IvannaFotoHoloBridge?.getSnapshotJson?.();
        if (raw) {
          const parsed = JSON.parse(raw) as { hrtfRuntime?: string };
          if (parsed.hrtfRuntime) {
            const engineActive = parsed.hrtfRuntime === 'reported-active';
            setParams(previous =>
              previous.hrtfEnabled === engineActive ? previous : { ...previous, hrtfEnabled: engineActive },
            );
          }
        }
      } catch {
        // Sin lectura nativa: se conserva el último estado conocido, nunca se simula.
      }
    };
    sync();
    const timer = window.setInterval(sync, 250);
    return () => { alive = false; window.clearInterval(timer); };
  }, []);

  return <main style={{ padding: 8, background: '#060a12' }}>
    <FotoHolo3DGenerator params={params} onParamChange={(key, value) => {
      setParams(previous => ({ ...previous, [key]: value }));
    }} />
  </main>;
}

createRoot(document.getElementById('root')!).render(
  <StrictMode><EmbeddedFotoHolo /></StrictMode>
);
