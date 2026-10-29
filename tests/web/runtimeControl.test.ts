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
