import { StrictMode, useState } from 'react';
import './index.css';
import { createRoot } from 'react-dom/client';
import { FotoHolo3DGenerator } from './components/FotoHolo3DGenerator';
import type { DspParameters } from './types';

const initial: DspParameters = {
  masterGain: 1, antiDolbyIntensity: 0, masterBypass: false, goldenEarEnabled: false,
  goldenEarDrive: 1, goldenEarMix: 0, fatigueIndex: 0, iirAlpha: 0.9, crosstalkGain: 0,
  hrtfDelayMs: 0, spatialAngleDeg: 0, spatialWidth: 1, spatialWetEta: 0,
  eqMutationRate: 0, sampleRate: 48000, blockSize: 512, nhoAlpha: 0, nhoBeta: 0,
  harmonicGain: 1, hrtfEnabled: false, adaptEnabled: false, compThresholdDb: -18,
  compRatio: 2, compAttackMs: 10, compReleaseMs: 100, activePreset: 'custom',
};

function EmbeddedFotoHolo() {
  const [params, setParams] = useState<DspParameters>(initial);
  return <main style={{ minHeight: '100vh', padding: 8, background: '#060a12' }}>
    <FotoHolo3DGenerator params={params} onParamChange={(key, value) => {
      setParams(previous => ({ ...previous, [key]: value }));
    }} />
  </main>;
}

createRoot(document.getElementById('root')!).render(
  <StrictMode><EmbeddedFotoHolo /></StrictMode>
);
