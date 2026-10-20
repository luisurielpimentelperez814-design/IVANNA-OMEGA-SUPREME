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
