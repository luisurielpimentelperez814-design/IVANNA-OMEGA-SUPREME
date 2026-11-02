import { test } from 'node:test';
import assert from 'node:assert/strict';
import { applyRuntimeControl } from '../../src/lib/runtimeControl.ts';

test('requires a supported command and matching finite readback', () => {
  let calls = 0;
  const bridge = { applyRuntimeControl: (_: string, value: number) => {
    calls++; return JSON.stringify({ accepted: true, readback: value });
  }};
  assert.equal(applyRuntimeControl(bridge, 'spatialWidth', 1.2).accepted, true);
  for (const [name,value] of [['unknown',1],['spatialWidth',NaN],['spatialWidth',2],['realityIntensity',-1],['realityEnabled',0.5]]) {
    assert.equal(applyRuntimeControl(bridge, String(name), value).accepted, false);
  }
  assert.equal(calls, 1);
  assert.equal(applyRuntimeControl(undefined, 'spatialWidth', 1).accepted, false);
});
test('rejects stale, absent, malformed or failed receipts', () => {
  for (const reply of ['{}','invalid','{"accepted":true,"readback":0}', '{"accepted":false,"readback":1}']) {
    assert.equal(applyRuntimeControl({ applyRuntimeControl: () => reply }, 'spatialWidth', 1).accepted, false);
  }
  assert.equal(applyRuntimeControl({ applyRuntimeControl: () => { throw Error('offline'); } }, 'spatialWidth', 1).accepted, false);
});

test('Acoustic Reality distinguishes daemon readback from global DSP generation', () => {
  const pending = applyRuntimeControl({
    applyRuntimeControl: (_: string, value: number) =>
      JSON.stringify({ accepted: true, readback: value, dspApplied: false, generation: 42 })
  }, 'realityEnabled', 1);
  assert.equal(pending.accepted, true);
  assert.equal(pending.dspApplied, false);
  assert.match(pending.message, /omega_effect aún no acredita/);

  const applied = applyRuntimeControl({
    applyRuntimeControl: (_: string, value: number) =>
      JSON.stringify({ accepted: true, readback: value, dspApplied: true, generation: 43 })
  }, 'realityIntensity', 0.75);
  assert.equal(applied.accepted, true);
  assert.equal(applied.dspApplied, true);
  assert.equal(applied.generation, 43);
  assert.match(applied.message, /generación 43 consumida por omega_effect/);
});
