import { test } from 'node:test';
import assert from 'node:assert/strict';
import { apiHeaders, setDashboardToken } from '../../src/lib/apiAccess.ts';

test('optional bearer is shared by chat and image clients and can be revoked', () => {
  setDashboardToken('');
  assert.equal(apiHeaders().Authorization, undefined);
  setDashboardToken(' test-dashboard-token ');
  assert.equal(apiHeaders().Authorization, 'Bearer test-dashboard-token');
  assert.equal(apiHeaders()['Content-Type'], 'application/json');
  setDashboardToken('');
  assert.equal(apiHeaders().Authorization, undefined);
});

test('invalid header characters and oversized tokens are rejected', () => {
  for (const token of ['a\r\nb', 'a b', 'é', 'a'.repeat(2049)]) {
    assert.throws(() => setDashboardToken(token));
  }
});
