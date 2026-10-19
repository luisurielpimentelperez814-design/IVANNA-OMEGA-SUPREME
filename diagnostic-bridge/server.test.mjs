import test from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';

test('bridge refuses to start without a strong token', async () => {
  const p = spawn(process.execPath, ['server.mjs'], { env: { ...process.env, IVANNA_DIAGNOSTIC_TOKEN: '' } });
  const [code] = await once(p, 'exit');
  assert.equal(code, 1);
});

test('analysis endpoint requires a snapshot and fails closed without an API key', async (t) => {
  const port = 20_000 + Math.floor(Math.random() * 20_000);
  const token = 'test-only-secret-token-that-is-long-enough';
  const p = spawn(process.execPath, ['server.mjs'], {
    cwd: new URL('.', import.meta.url),
    env: { ...process.env, PORT: String(port), IVANNA_DIAGNOSTIC_TOKEN: token, OPENAI_API_KEY: '' },
    stdio: 'ignore'
  });
  t.after(() => p.kill('SIGTERM'));
  const base = 'http://127.0.0.1:' + port;
  let ready = false;
  for (let i = 0; i < 40; i++) {
    try {
      const r = await fetch(base + '/healthz');
      if (r.ok) { ready = true; break; }
    } catch {}
    await new Promise(resolve => setTimeout(resolve, 50));
  }
  assert.equal(ready, true, 'bridge should start');
  const auth = { authorization: 'Bearer ' + token, 'content-type': 'application/json' };
  const noSnapshot = await fetch(base + '/v1/diagnostics/analyze', { method: 'POST', headers: auth });
  assert.equal(noSnapshot.status, 404);

  const snapshot = {
    schemaVersion: 1, sequence: 1, uptimeMs: 1000,
    metrics: { dspProcessingUs: 120, frameDrops: 0, underruns: 0, thermalThrottling: false, memoryPressure: 'normal' },
    agent: { running: true, daemonConnected: true, adaptiveEngineRunning: true, clipping: false, latencyDegraded: false, clipCount: 0, roundTripLatencyUs: 4000 }
  };
  const upload = await fetch(base + '/v1/diagnostics', { method: 'POST', headers: auth, body: JSON.stringify(snapshot) });
  assert.equal(upload.status, 202);
  const analysis = await fetch(base + '/v1/diagnostics/analyze', { method: 'POST', headers: auth });
  assert.equal(analysis.status, 503);
  assert.equal((await analysis.json()).error, 'ai_not_configured');
});
