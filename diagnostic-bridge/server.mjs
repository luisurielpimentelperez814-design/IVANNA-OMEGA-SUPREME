#!/usr/bin/env node
/**
 * IVANNA live diagnostic bridge.
 * Stateless except for the latest in-memory snapshot; no audio or personal identifiers.
 * Terminate TLS at the hosting platform. Refuses to start without a strong bearer token.
 */
import http from 'node:http';
import { randomUUID, timingSafeEqual } from 'node:crypto';

const PORT = Number(process.env.PORT || 8787);
const TOKEN = process.env.IVANNA_DIAGNOSTIC_TOKEN || '';
if (TOKEN.length < 32) {
  console.error('IVANNA_DIAGNOSTIC_TOKEN must be at least 32 characters; refusing to start.');
  process.exit(1);
}
const MAX_BODY = 16 * 1024;
const WINDOW_MS = 60_000;
const MAX_REQUESTS = 60;
const buckets = new Map();
let latest = null;

function authorized(req) {
  const header = req.headers.authorization || '';
  if (!header.startsWith('Bearer ')) return false;
  const candidate = Buffer.from(header.slice(7));
  const expected = Buffer.from(TOKEN);
  return candidate.length === expected.length && timingSafeEqual(candidate, expected);
}
function rateAllowed(ip) {
  const now = Date.now();
  let b = buckets.get(ip);
  if (!b || now - b.start >= WINDOW_MS) { b = { start: now, count: 0 }; buckets.set(ip, b); }
  b.count++;
  return b.count <= MAX_REQUESTS;
}
function json(res, status, data) {
  res.writeHead(status, { 'content-type': 'application/json; charset=utf-8',
    'cache-control': 'no-store', 'x-content-type-options': 'nosniff',
    'referrer-policy': 'no-referrer' });
  res.end(JSON.stringify(data));
}
function finite(value, min, max) {
  return typeof value === 'number' && Number.isFinite(value) && value >= min && value <= max;
}
function sanitize(s) {
  if (!s || typeof s !== 'object' || Array.isArray(s)) return null;
  const m = s.metrics || {}, a = s.agent || {};
  if (s.schemaVersion !== 1 || !finite(s.sequence, 0, Number.MAX_SAFE_INTEGER) ||
      !finite(s.uptimeMs, 0, Number.MAX_SAFE_INTEGER)) return null;
  const out = {
    schemaVersion: 1, sequence: s.sequence, uptimeMs: s.uptimeMs,
    observedAt: new Date().toISOString(),
    metrics: {
      dspProcessingUs: finite(m.dspProcessingUs, 0, 10_000_000) ? m.dspProcessingUs : null,
      frameDrops: finite(m.frameDrops, 0, 1_000_000_000) ? m.frameDrops : null,
      underruns: finite(m.underruns, 0, 1_000_000_000) ? m.underruns : null,
      thermalThrottling: typeof m.thermalThrottling === 'boolean' ? m.thermalThrottling : null,
      memoryPressure: ['normal','moderate','high','unknown'].includes(m.memoryPressure) ? m.memoryPressure : 'unknown'
    },
    agent: {
      running: typeof a.running === 'boolean' ? a.running : null,
      daemonConnected: typeof a.daemonConnected === 'boolean' ? a.daemonConnected : null,
      adaptiveEngineRunning: typeof a.adaptiveEngineRunning === 'boolean' ? a.adaptiveEngineRunning : null,
      clipping: typeof a.clipping === 'boolean' ? a.clipping : null,
      latencyDegraded: typeof a.latencyDegraded === 'boolean' ? a.latencyDegraded : null,
      clipCount: finite(a.clipCount, 0, 1_000_000_000) ? a.clipCount : null,
      roundTripLatencyUs: finite(a.roundTripLatencyUs, 0, 60_000_000) ? a.roundTripLatencyUs : null
    }
  };
  return out;
}
const server = http.createServer(async (req, res) => {
  if (req.method === 'GET' && req.url === '/healthz') return json(res, 200, { ok: true, service: 'ivanna-diagnostic-bridge' });
  if (!['POST /v1/diagnostics', 'GET /v1/diagnostics/latest'].includes(req.method + ' ' + req.url))
    return json(res, 404, { error: 'not_found' });
  if (!authorized(req)) return json(res, 401, { error: 'unauthorized' });
  const ip = req.socket.remoteAddress || 'unknown';
  if (!rateAllowed(ip)) return json(res, 429, { error: 'rate_limited' });
  if (req.method === 'GET') return latest ? json(res, 200, latest) : json(res, 404, { error: 'no_snapshot_yet' });
  let raw = '';
  try {
    for await (const chunk of req) {
      raw += chunk;
      if (Buffer.byteLength(raw) > MAX_BODY) return json(res, 413, { error: 'payload_too_large' });
    }
    const safe = sanitize(JSON.parse(raw));
    if (!safe) return json(res, 400, { error: 'invalid_snapshot' });
    latest = { ...safe, receivedAt: new Date().toISOString(), snapshotId: randomUUID() };
    return json(res, 202, { accepted: true, sequence: safe.sequence, receivedAt: latest.receivedAt });
  } catch {
    return json(res, 400, { error: 'invalid_json' });
  }
});
server.listen(PORT, '0.0.0.0', () => console.log('IVANNA diagnostic bridge listening on ' + PORT));
