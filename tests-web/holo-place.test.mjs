// Ejecutar: node --test tests-web/   (Node >= 22.18, sin dependencias)
import test from 'node:test';
import assert from 'node:assert/strict';
import { derivePlaceSpec, buildPlacePrimitives, hashSeed } from '../src/lib/proceduralHoloPlace.ts';

test('es determinista: misma entrada, misma escena', () => {
  const a = derivePlaceSpec('Catedral gótica', 'classical', 'hall');
  const b = derivePlaceSpec('Catedral gótica', 'classical', 'hall');
  assert.deepEqual(a, b);
  assert.deepEqual(buildPlacePrimitives(a, 0.4), buildPlacePrimitives(b, 0.4));
});

test('las palabras clave deciden la familia y se reportan', () => {
  assert.equal(derivePlaceSpec('Wembley en los 80').family, 'stadium');
  assert.equal(derivePlaceSpec('club de jazz íntimo').family, 'club');
  assert.equal(derivePlaceSpec('estudio Abbey Road').family, 'studio');
  const c = derivePlaceSpec('iglesia de piedra');
  assert.equal(c.family, 'cathedral');
  assert.ok(c.matched.includes('iglesia'));
});

test('sin palabra reconocida cae en sala genérica y lo declara (matched vacío)', () => {
  const s = derivePlaceSpec('xyzzy');
  assert.equal(s.family, 'hall');
  assert.deepEqual(s.matched, []);
});

test('textos distintos dan semillas y dimensiones distintas', () => {
  assert.notEqual(hashSeed('a'), hashSeed('b'));
  const a = derivePlaceSpec('club de jazz uno');
  const b = derivePlaceSpec('club de jazz dos');
  assert.notDeepEqual([a.halfWidth, a.halfDepth, a.height], [b.halfWidth, b.halfDepth, b.height]);
});

test('toda primitiva es finita y los colores están en [0,1]', () => {
  for (const q of ['catedral', 'estadio', 'bosque', 'planetario', 'cueva', 'bodega rave', '']) {
    const spec = derivePlaceSpec(q, 'rock-80s', 'Escenario de concierto');
    for (const energy of [null, 0, 1, 7, -3]) {
      const prims = buildPlacePrimitives(spec, energy);
      assert.ok(prims.length > 3, q);
      for (const p of prims) {
        const nums = Object.entries(p).filter(([k, v]) => typeof v === 'number').map(([, v]) => v);
        assert.ok(nums.every(Number.isFinite), JSON.stringify(p));
        assert.ok(p.color.every(c => c >= 0 && c <= 1), JSON.stringify(p));
      }
    }
  }
});

test('la energía solo cambia el brillo, no la geometría', () => {
  const spec = derivePlaceSpec('teatro de ópera');
  const strip = ps => ps.map(({ color, ...g }) => g);
  assert.deepEqual(strip(buildPlacePrimitives(spec, 0)), strip(buildPlacePrimitives(spec, 1)));
});

import { parseCatalog, pickCatalogRoom, FAMILY_TYPICAL } from '../src/lib/proceduralHoloPlace.ts';

const CATALOG = [
  { idx: 0, rt60S: 0.30, volumeM3: 60, w: 4, h: 2.6, d: 5 },
  { idx: 1, rt60S: 0.70, volumeM3: 350, w: 8, h: 4.2, d: 10 },
  { idx: 2, rt60S: 2.50, volumeM3: 1200, w: 14, h: 5.9, d: 19 },
];

test('parseCatalog descarta filas dudosas y JSON roto, sin inventar valores', () => {
  assert.deepEqual(parseCatalog('no json'), []);
  assert.deepEqual(parseCatalog('{"a":1}'), []);
  const rows = parseCatalog('[[1,0.5,100,5,3,6],[2,null,1,1,1,1],[3,0.5,-1,1,1,1],[4,0.5,100,5,3]]');
  assert.equal(rows.length, 1);
  assert.equal(rows[0].idx, 1);
});

test('elige la sala del catálogo más cercana a la familia y usa sus dimensiones reales', () => {
  assert.equal(pickCatalogRoom('studio', CATALOG).idx, 0);
  assert.equal(pickCatalogRoom('club', CATALOG).idx, 1);
  assert.equal(pickCatalogRoom('cathedral', CATALOG).idx, 2);
  assert.equal(pickCatalogRoom('hall', []), null);
  const spec = derivePlaceSpec('club de jazz', '', '', CATALOG);
  assert.equal(spec.source, 'rir-catalog');
  assert.equal(spec.catalogRoom.idx, 1);
  assert.equal(spec.halfWidth, 4);        // 8 m / 2
  assert.equal(spec.height, 4.2);
});

test('declara la brecha de RT60 cuando el catálogo no alcanza a la familia (catedral)', () => {
  const spec = derivePlaceSpec('catedral', '', '', CATALOG);
  assert.equal(spec.catalogRoom.idx, 2);
  assert.ok(spec.rt60GapS > 2, 'catedral típica ≈5 s vs sala 2.5 s: la brecha debe verse');
  assert.equal(spec.familyRt60S, FAMILY_TYPICAL.cathedral.rt60S);
});

test('sin catálogo vuelve al modo paramétrico y lo dice', () => {
  const spec = derivePlaceSpec('club de jazz');
  assert.equal(spec.source, 'local-procedural');
  assert.equal(spec.catalogRoom, null);
  assert.equal(spec.rt60GapS, null);
});
