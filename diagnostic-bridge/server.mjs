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
async function analyzeWithOpenAI(snapshot) {
  const apiKey = process.env.OPENAI_API_KEY || '';
  if (!apiKey) return { status: 503, body: { error: 'ai_not_configured', detail: 'Set OPENAI_API_KEY on the server; never place it in the app.' } };
  const model = process.env.OPENAI_MODEL || 'gpt-4.1-mini';
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 12_000);
  try {
    const response = await fetch('https://api.openai.com/v1/responses', {
      method: 'POST',
      headers: { authorization: 'Bearer ' + apiKey, 'content-type': 'application/json' },
      signal: controller.signal,
      body: JSON.stringify({
        model,
        instructions: 'You are the diagnostic analyst for IVANNA OMEGA. Analyze only the supplied sanitized telemetry snapshot. Return concise Spanish with: estado, hallazgos respaldados por métricas, incertidumbres, and recommended next diagnostic checks. Never claim a repair was performed, never issue commands, never infer personal identity, and never recommend changing settings automatically. Distinguish evidence from hypotheses.',
        input: JSON.stringify(snapshot),
        max_output_tokens: 700
      })
    });
    const data = await response.json().catch(() => ({}));
    if (!response.ok) return { status: 502, body: { error: 'ai_provider_error', providerStatus: response.status } };
    const answer = (data.output || []).flatMap(item => item.content || [])
      .filter(part => part.type === 'output_text' && typeof part.text === 'string')
      .map(part => part.text).join('\n').trim();
    if (!answer) return { status: 502, body: { error: 'ai_empty_response' } };
    return { status: 200, body: { ok: true, model, snapshotId: snapshot.snapshotId, analysis: answer, repairAuthority: 'recommendations_only' } };
  } catch (error) {
    return { status: 502, body: { error: error?.name === 'AbortError' ? 'ai_timeout' : 'ai_unavailable' } };
  } finally {
    clearTimeout(timeout);
  }
}
const server = http.createServer(async (req, res) => {
  if (req.method === 'GET' && req.url === '/healthz') return json(res, 200, { ok: true, service: 'ivanna-diagnostic-bridge' });
  if (!['POST /v1/diagnostics', 'GET /v1/diagnostics/latest', 'POST /v1/diagnostics/analyze'].includes(req.method + ' ' + req.url))
    return json(res, 404, { error: 'not_found' });
  if (!authorized(req)) return json(res, 401, { error: 'unauthorized' });
  const ip = req.socket.remoteAddress || 'unknown';
  if (!rateAllowed(ip)) return json(res, 429, { error: 'rate_limited' });
  if (req.method === 'GET') return latest ? json(res, 200, latest) : json(res, 404, { error: 'no_snapshot_yet' });
  if (req.method === 'POST' && req.url === '/v1/diagnostics/analyze') {
    if (!latest) return json(res, 404, { error: 'no_snapshot_yet' });
    const result = await analyzeWithOpenAI(latest);
    return json(res, result.status, result.body);
  }
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
