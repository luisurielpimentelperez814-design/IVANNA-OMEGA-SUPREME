import { test } from 'node:test';
import assert from 'node:assert/strict';
import { composeEnvironmentLocal, refineSceneLocal, type FotoHoloTelemetryFeatures } from '../../src/lib/fotoholoLocal.ts';

const fresh = (over: Partial<FotoHoloTelemetryFeatures> = {}): FotoHoloTelemetryFeatures => ({
  energy: 0.5, flux: 0.3, lowShare: 0.4, highShare: 0.4, valid: true, fresh: true, ...over,
});

test('refine local: graves dominantes abren la anchura y eligen concierto', () => {
  const r = refineSceneLocal(fresh({ lowShare: 0.7, highShare: 0.2 }), 'rock-70s', 'unknown', { preset: 'nanotomix', rotation: 0, elevation: 18 });
  assert.equal(r.preset, 'concert');
  assert.ok(r.width > 53, `anchura ${r.width} debe superar el neutro 53 con graves dominantes`);
  assert.ok(r.width >= 20 && r.width <= 110);
  assert.ok(r.rationale.includes('Local'));
});

test('refine local: agudos dominantes cierran la anchura y eligen estudio', () => {
  const r = refineSceneLocal(fresh({ lowShare: 0.2, highShare: 0.7 }), 'jazz', 'unknown', { preset: 'nanotomix', rotation: 0, elevation: 18 });
  assert.equal(r.preset, 'studio');
  assert.ok(r.width < 53, `anchura ${r.width} debe quedar bajo el neutro 53 con agudos dominantes`);
});

test('refine local: sin telemetría fresca usa género y entorno, sin inventar espectro', () => {
  const r = refineSceneLocal({ energy: 0, flux: 0, lowShare: 0, highShare: 0, valid: false, fresh: false }, 'electronic', 'arena', { preset: 'nanotomix', rotation: 10, elevation: 18 });
  assert.equal(r.preset, 'concert');
  assert.equal(r.rotation, 10, 'la rotación se conserva, no se gira al usuario');
  assert.ok(r.rationale.includes('sin telemetría fresca'));
});

test('refine local: todo acotado aunque el espectro sea extremo', () => {
  const r = refineSceneLocal(fresh({ energy: 1, flux: 1, lowShare: 1, highShare: 1 }), 'hip-hop', 'arena', { preset: 'nanotomix', rotation: 200, elevation: 100 });
  assert.ok(r.width >= 20 && r.width <= 110);
  assert.ok(r.depth >= 20 && r.depth <= 110);
  assert.ok(r.height >= 20 && r.height <= 100);
  assert.ok(r.rotation >= -180 && r.rotation <= 180);
  assert.ok(r.elevation >= -50 && r.elevation <= 65);
});

test('compose local: Wembley es estadio, club de jazz es club, Abbey es estudio', () => {
  assert.equal(composeEnvironmentLocal('Wembley en los 80', 'rock-70s', 'unknown').palette, 'arena');
  assert.equal(composeEnvironmentLocal('club de jazz íntimo', 'jazz', 'unknown').palette, 'club');
  assert.equal(composeEnvironmentLocal('estudio Abbey Road', 'rock-70s', 'unknown').palette, 'studio');
  assert.equal(composeEnvironmentLocal('teatro de ópera', 'opera-vocal', 'unknown').palette, 'theatre');
  assert.equal(composeEnvironmentLocal('festival al aire libre', 'electronic', 'unknown').palette, 'outdoor');
});

test('compose local: sin palabra clave manda el entorno de escucha elegido', () => {
  assert.equal(composeEnvironmentLocal('mi lugar', 'rock-70s', 'arena').preset, 'concert');
  assert.equal(composeEnvironmentLocal('mi lugar', 'jazz', 'treated-studio').preset, 'studio');
  assert.equal(composeEnvironmentLocal('', 'rock-70s', 'unknown').preset, 'nanotomix');
});
