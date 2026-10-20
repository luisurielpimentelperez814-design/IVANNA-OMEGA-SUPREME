/**
 * Generador procedimental LOCAL de entornos 3D para FotoHolo.
 *
 * No usa red, IA remota, claves ni cuotas: convierte el texto del lugar (+ género/entorno) en una especificación
 * arquitectónica determinista (misma entrada → misma escena) y la expande a primitivas 3D que el WebGL ya sabe dibujar.
 * Es una SÍNTESIS paramétrica a partir de palabras clave: NO reconstruye el lugar real ni mide su acústica.
 * Módulo puro y sin imports para probarse con `node --test` (ver tests-web/).
 */
export type V3 = [number, number, number];
export type PlaceFamily =
  | 'cathedral' | 'stadium' | 'club' | 'theater' | 'studio' | 'outdoor' | 'warehouse' | 'cave' | 'orbital' | 'hall';

export type PlaceSpec = {
  source: 'local-procedural';
  seed: number;
  family: PlaceFamily;
  matched: string[];            // palabras clave que decidieron la familia (vacío = valor por defecto)
  halfWidth: number;            // semiancho del recinto (unidades de escena)
  halfDepth: number;
  height: number;
  columns: number;              // columnas por lado
  tiers: number;                // gradas / balcones
  vaulted: boolean;
  palette: { base: V3; accent: V3; glow: V3 };
};

export type PlacePrimitive =
  | { kind: 'box'; x: number; y: number; z: number; w: number; h: number; d: number; color: V3 }
  | { kind: 'ellipsoid'; x: number; y: number; z: number; rx: number; ry: number; rz: number; color: V3; wire: boolean };

const FAMILY_KEYWORDS: Array<[PlaceFamily, string[]]> = [
  ['cathedral', ['catedral', 'iglesia', 'church', 'cathedral', 'basilica', 'basílica', 'templo', 'abadia', 'abadía']],
  ['stadium', ['estadio', 'stadium', 'arena', 'wembley', 'coliseo', 'plaza de toros', 'foro sol']],
  ['theater', ['teatro', 'theatre', 'theater', 'opera', 'ópera', 'auditorio', 'sala de conciertos', 'philharmonic', 'filarmonica', 'filarmónica']],
  ['club', ['club', 'jazz', 'bar ', 'cafe', 'café', 'pub', 'lounge', 'cabaret']],
  ['studio', ['estudio', 'studio', 'abbey road', 'cabina', 'grabacion', 'grabación']],
  ['warehouse', ['bodega', 'warehouse', 'rave', 'nave industrial', 'fabrica', 'fábrica', 'sotano', 'sótano']],
  ['outdoor', ['aire libre', 'bosque', 'playa', 'outdoor', 'festival', 'montaña', 'montana', 'desierto', 'parque', 'jungla', 'campo']],
  ['cave', ['cueva', 'cave', 'caverna', 'gruta', 'tunel', 'túnel']],
  ['orbital', ['espacio', 'orbital', 'planetario', 'nave espacial', 'galaxia', 'luna', 'estacion espacial', 'estación espacial']],
];

/** FNV-1a de 32 bits sobre el texto normalizado. */
export function hashSeed(text: string): number {
  const t = text.toLowerCase().normalize('NFC').trim();
  let h = 0x811c9dc5;
  for (let i = 0; i < t.length; i++) {
    h ^= t.charCodeAt(i);
    h = Math.imul(h, 0x01000193) >>> 0;
  }
  return h >>> 0;
}

/** PRNG mulberry32 → [0,1). Determinista por semilla. */
export function makeRng(seed: number): () => number {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function pickFamily(text: string): { family: PlaceFamily; matched: string[] } {
  const t = ' ' + text.toLowerCase() + ' ';
  for (const [family, words] of FAMILY_KEYWORDS) {
    const hit = words.filter(w => t.includes(w));
    if (hit.length) return { family, matched: hit };
  }
  return { family: 'hall', matched: [] };
}

const clamp = (v: number, lo: number, hi: number) => Math.max(lo, Math.min(hi, v));
const hsv = (h: number, s: number, v: number): V3 => {
  const f = (n: number) => { const k = (n + h * 6) % 6; return v - v * s * Math.max(0, Math.min(k, 4 - k, 1)); };
  return [f(5), f(3), f(1)];
};

/** Familia → proporciones base [semiancho, semiprofundidad, alto, columnas, gradas, bóveda, tono]. */
const BASE: Record<PlaceFamily, [number, number, number, number, number, boolean, number]> = {
  cathedral: [3.2, 7.5, 6.5, 5, 0, true, 0.08],
  stadium:   [9.0, 9.0, 4.0, 0, 4, false, 0.55],
  theater:   [4.5, 6.0, 3.6, 3, 3, true, 0.02],
  club:      [3.0, 3.6, 2.2, 2, 1, false, 0.78],
  studio:    [2.6, 3.2, 2.0, 0, 0, false, 0.12],
  warehouse: [5.5, 7.0, 3.8, 4, 0, false, 0.6],
  outdoor:   [10.0, 10.0, 7.0, 0, 0, false, 0.36],
  cave:      [4.0, 6.5, 3.5, 0, 0, true, 0.05],
  orbital:   [6.0, 6.0, 6.0, 0, 0, true, 0.62],
  hall:      [4.0, 5.0, 3.0, 2, 1, false, 0.5],
};

export function derivePlaceSpec(prompt: string, genreLabel = '', venueLabel = ''): PlaceSpec {
  const text = (prompt || '').trim() || venueLabel;
  const { family, matched } = pickFamily(text + ' ' + venueLabel);
  const seed = hashSeed(text + '|' + genreLabel + '|' + venueLabel);
  const rnd = makeRng(seed);
  const [bw, bd, bh, bc, bt, bv, hue] = BASE[family];
  const jitter = (v: number, amt: number) => v * (1 + (rnd() - 0.5) * 2 * amt);
  return {
    source: 'local-procedural',
    seed, family, matched,
    halfWidth: clamp(jitter(bw, 0.18), 1.8, 11),
    halfDepth: clamp(jitter(bd, 0.18), 2.2, 11),
    height: clamp(jitter(bh, 0.15), 1.6, 8),
    columns: Math.max(0, Math.round(bc + (rnd() - 0.5) * 2)),
    tiers: Math.max(0, Math.round(bt + (rnd() - 0.5))),
    vaulted: bv,
    palette: {
      base: hsv((hue + rnd() * 0.06) % 1, 0.55, 0.22),
      accent: hsv((hue + 0.08 + rnd() * 0.06) % 1, 0.7, 0.6),
      glow: hsv((hue + 0.5 + rnd() * 0.08) % 1, 0.65, 0.95),
    },
  };
}

/**
 * Expande la especificación a primitivas. `energy` ∈ [0,1] (o null sin señal) solo modula el brillo de las
 * luminarias; la geometría no depende del audio, así que la escena es estable y reproducible.
 */
export function buildPlacePrimitives(spec: PlaceSpec, energy: number | null = null): PlacePrimitive[] {
  const out: PlacePrimitive[] = [];
  const rnd = makeRng(spec.seed ^ 0x9e3779b9);
  const { halfWidth: W, halfDepth: D, height: H, palette: P } = spec;
  const e = energy === null ? 0.35 : clamp(energy, 0, 1);
  const lift = (c: V3, k: number): V3 => [clamp(c[0] * k, 0, 1), clamp(c[1] * k, 0, 1), clamp(c[2] * k, 0, 1)];
  const box = (x: number, y: number, z: number, w: number, h: number, d: number, color: V3) =>
    out.push({ kind: 'box', x, y, z, w, h, d, color });
  const ell = (x: number, y: number, z: number, rx: number, ry: number, rz: number, color: V3, wire: boolean) =>
    out.push({ kind: 'ellipsoid', x, y, z, rx, ry, rz, color, wire });

  const floorY = -2.2;
  box(0, floorY, 0, W * 2, 0.1, D * 2, P.base);
  if (spec.family !== 'outdoor' && spec.family !== 'orbital') {
    box(-W, floorY + H / 2, 0, 0.12, H, D * 2, lift(P.base, 0.8));
    box(W, floorY + H / 2, 0, 0.12, H, D * 2, lift(P.base, 0.8));
    box(0, floorY + H / 2, -D, W * 2, H, 0.12, lift(P.base, 0.9));
    if (spec.vaulted) ell(0, floorY + H, 0, W, H * 0.55, D, P.accent, true);
    else box(0, floorY + H, 0, W * 2, 0.1, D * 2, lift(P.base, 0.7));
  }
  // Columnas en dos filas, separadas uniformemente a lo largo de la profundidad.
  for (let i = 0; i < spec.columns; i++) {
    const z = -D + ((i + 1) * (D * 2)) / (spec.columns + 1);
    for (const s of [-1, 1]) box(s * W * 0.72, floorY + H / 2, z, 0.22, H, 0.22, lift(P.accent, 0.8));
  }
  // Gradas / balcones escalonados.
  for (let t = 0; t < spec.tiers; t++) {
    const y = floorY + 0.25 + t * 0.45, inset = t * 0.55;
    for (const s of [-1, 1]) box(s * (W - 0.4 - inset * 0.3), y, 0, 0.7, 0.3, D * 1.6 - inset, lift(P.accent, 0.55 + t * 0.07));
    box(0, y, -D + 0.5 + inset * 0.3, W * 1.6 - inset, 0.3, 0.7, lift(P.accent, 0.55 + t * 0.07));
  }
  // Escenario con luminarias cuyo brillo sigue la energía real (si hay señal).
  box(0, floorY + 0.25, -D * 0.55, W * 0.9, 0.3, D * 0.28, lift(P.accent, 0.9));
  const lamps = 3 + Math.floor(rnd() * 3);
  for (let i = 0; i < lamps; i++) {
    const x = -W * 0.4 + (i * W * 0.8) / Math.max(1, lamps - 1);
    ell(x, floorY + H * 0.8, -D * 0.5, 0.16, 0.16, 0.16, lift(P.glow, 0.7 + e * 0.5), false);
  }
  if (spec.family === 'outdoor') {
    // Horizonte de formas orgánicas deterministas.
    for (let i = 0; i < 9; i++) {
      const x = (rnd() - 0.5) * W * 2, z = -D * (0.7 + rnd() * 0.3), r = 0.8 + rnd() * 1.6;
      ell(x, floorY + r * 0.5, z, r * 1.4, r * 0.6, r, lift(P.base, 1.2), true);
    }
  }
  if (spec.family === 'orbital') {
    for (let k = 1; k <= 3; k++) ell(0, 0, 0, W * k / 3, W * k / 6, W * k / 3, lift(P.glow, 0.5 + k * 0.15), true);
  }
  return out;
}
