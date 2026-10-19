import test from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';

test('bridge refuses to start without a strong token', async () => {
  const p = spawn(process.execPath, ['server.mjs'], { env: { ...process.env, IVANNA_DIAGNOSTIC_TOKEN: '' } });
  const [code] = await once(p, 'exit');
  assert.equal(code, 1);
});
