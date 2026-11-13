/**
 * FotoHolo LOCAL (v2.5.428): refinación y composición de entorno 100% local.
 * Sin Gemini, sin red, sin cuota, sin clave. Reglas deterministas de ingeniero
 * de mezcla desde la telemetría nativa (energía Bark, flux, low/high share) +
 * género + entorno. El ajuste es conservador, reversible y visible; la anchura
 * y la rotación se envían al motor nativo (impacto audible real: la anchura
 * espacial cambia lo que se oye). Puro y testeable: sin I/O, sin red.
 */
import type { GenreId, VenueId } from './fotoholoAcousticKnowledge';

export type FotoHoloPreset = 'nanotomix' | 'concert' | 'studio' | 'orbital';
export type FotoHoloPalette = 'arena' | 'club' | 'studio' | 'theatre' | 'outdoor' | 'neutral';

export interface FotoHoloTelemetryFeatures {
  energy: number; // 0..1 energía Bark
  flux: number; // 0..1 transitorios
  lowShare: number; // 0..1 proporción de graves
  highShare: number; // 0..1 proporción de agudos
  valid: boolean;
  fresh: boolean; // muestra < 1000 ms
}

export interface RefineResult {
  preset: FotoHoloPreset;
  width: number; // 20..110 (anchura espacial · DSP)
  depth: number; // 20..110 (visual)
  height: number; // 20..100 (visual)
  rotation: number; // -180..180 (rotación · DSP)
  elevation: number; // -50..65 (visual)
  rationale: string;
}

export interface ComposeResult {
  preset: FotoHoloPreset;
  label: string; // nombre del entorno compuesto
  palette: FotoHoloPalette;
  rationale: string;
}

const clamp = (v: number, lo: number, hi: number): number =>
  Math.max(lo, Math.min(hi, v));

/** Paletas de luz del entorno: color de acento (haces/fuentes) y de onda. */
export const FOTOHOLO_PALETTES: Record<FotoHoloPalette, { accent: [number, number, number]; wave: [number, number, number] }> = {
  arena: { accent: [0.3, 0.8, 0.95], wave: [0.05, 0.65, 0.92] },
  club: { accent: [0.95, 0.45, 0.75], wave: [0.85, 0.25, 0.55] },
  studio: { accent: [0.55, 0.85, 0.95], wave: [0.2, 0.7, 0.85] },
  theatre: { accent: [0.95, 0.8, 0.45], wave: [0.75, 0.6, 0.25] },
  outdoor: { accent: [0.45, 0.85, 0.55], wave: [0.25, 0.75, 0.45] },
  neutral: { accent: [0.3, 0.8, 0.95], wave: [0.05, 0.65, 0.92] },
};

/**
 * Refinación local de la escena. Sin telemetría fresca usa solo género y
 * entorno (corrección mínima); con telemetría fresca usa el espectro vivo.
 * Nunca inventa mediciones: sin datos, no ajusta por espectro.
 */
export function refineSceneLocal(
  features: FotoHoloTelemetryFeatures,
  genre: GenreId,
  venue: VenueId,
  current: { preset: FotoHoloPreset; rotation: number; elevation: number },
): RefineResult {
  const { energy, flux, lowShare, highShare, valid, fresh } = features;
  const live = valid && fresh;

  // Preset: el entorno manda; sin entorno claro, manda el espectro vivo.
  let preset: FotoHoloPreset;
  if (venue === 'arena' || venue === 'club') preset = 'concert';
  else if (venue === 'treated-studio' || venue === 'nearfield') preset = 'studio';
  else if (venue === 'outdoor') preset = 'orbital';
  else if (live && lowShare > 0.55) preset = 'concert';
  else if (live && highShare > 0.55) preset = 'studio';
  else preset = 'nanotomix';

  // Anchura (DSP): graves anchos, agudos estrechos; transitorios la abren.
  // Género: jazz/clásica/podcast la cierran (imagen estable); electrónica/
  // hip-hop la abren (energía). Todo acotado, reversible.
  let width = live ? 50 + lowShare * 18 - highShare * 8 + flux * 6 : 53;
  if (genre === 'jazz' || genre === 'classical' || genre === 'opera-vocal' || genre === 'podcast-speech') width -= 8;
  else if (genre === 'electronic' || genre === 'hip-hop' || genre === 'house-techno' || genre === 'reggaeton') width += 8;
  width = clamp(Math.round(width), 20, 110);

  // Profundidad: energía viva la abre; arena suma, estudio resta.
  let depth = live ? 60 + energy * 10 : 65;
  if (venue === 'arena') depth += 10;
  else if (venue === 'treated-studio') depth -= 10;
  depth = clamp(Math.round(depth), 20, 110);

  // Altura: agudos dominantes la suben.
  const height = clamp(Math.round(live ? 50 + highShare * 15 : 55), 20, 100);

  // Rotación: no se gira al usuario; se conserva la actual.
  const rotation = clamp(Math.round(current.rotation), -180, 180);

  // Elevación: el desbalance agudos-graves la inclina, acotada.
  const elevation = clamp(
    Math.round(live ? current.elevation + (highShare - lowShare) * 8 : current.elevation),
    -50,
    65,
  );

  const rationale = live
    ? `Local: graves ${(lowShare * 100).toFixed(0)}% · agudos ${(highShare * 100).toFixed(0)}% · energía ${(energy * 100).toFixed(0)}% → ${preset}, anchura ${width}.`
    : `Local: sin telemetría fresca; ajuste mínimo por género y entorno → ${preset}, anchura ${width}.`;

  return { preset, width, depth, height, rotation, elevation, rationale };
}

const PLACE_KEYWORDS: Array<{ re: RegExp; preset: FotoHoloPreset; label: string; palette: FotoHoloPalette }> = [
  { re: /estadio|wembley|arena|concierto masivo|coliseo|foro sol/i, preset: 'concert', label: 'Estadio / arena', palette: 'arena' },
  { re: /club|jazz|intimo|íntimo|bar|pub|antro|discoteca/i, preset: 'concert', label: 'Club íntimo', palette: 'club' },
  { re: /estudio|abbey|grabaci[oó]n|mezcla|cabina/i, preset: 'studio', label: 'Estudio de grabación', palette: 'studio' },
  { re: /teatro|auditorio|opera|ópera|cine|sala elite|concierto cl[aá]sico/i, preset: 'nanotomix', label: 'Teatro / sala élite', palette: 'theatre' },
  { re: /exterior|festival|playa|parque|monta[nñ]a|campo|aire libre/i, preset: 'orbital', label: 'Exterior / festival', palette: 'outdoor' },
];

/**
 * Composición LOCAL del entorno: el lugar escrito + género + entorno eligen
 * un arquetipo de recinto (preset + paleta de luz) que la escena 3D renderiza
 * en vivo. No genera imagen por IA: el entorno ES la escena 3D interactiva
 * (orbitable, exportable como PNG). Sin red, sin cuota, instantáneo.
 */
export function composeEnvironmentLocal(
  place: string,
  genre: GenreId,
  venue: VenueId,
): ComposeResult {
  const text = place.trim();
  for (const k of PLACE_KEYWORDS) {
    if (k.re.test(text)) {
      return {
        preset: k.preset,
        label: k.label,
        palette: k.palette,
        rationale: `Entorno local: "${text}" → ${k.label} (${k.preset}). Escena 3D compuesta en el dispositivo, sin nube.`,
      };
    }
  }
  // Sin palabra clave: manda el entorno de escucha elegido.
  if (venue === 'arena' || venue === 'club') {
    return { preset: 'concert', label: 'Concierto inmersivo', palette: 'arena', rationale: `Entorno local por sala: ${venue} → concierto inmersivo.` };
  }
  if (venue === 'treated-studio' || venue === 'nearfield') {
    return { preset: 'studio', label: 'Estudio de precisión', palette: 'studio', rationale: `Entorno local por sala: ${venue} → estudio de precisión.` };
  }
  if (venue === 'theatre') {
    return { preset: 'nanotomix', label: 'Teatro / sala élite', palette: 'theatre', rationale: 'Entorno local por sala: teatro / sala élite.' };
  }
  if (venue === 'outdoor') {
    return { preset: 'orbital', label: 'Exterior / festival', palette: 'outdoor', rationale: 'Entorno local por sala: exterior / festival.' };
  }
  // Sin palabra clave ni sala: manda el género (arquetipo por energía).
  if (genre === 'electronic' || genre === 'hip-hop' || genre === 'house-techno' || genre === 'reggaeton' || genre === 'rock-80s') {
    return { preset: 'concert', label: 'Concierto inmersivo', palette: 'arena', rationale: `Entorno local por género: ${genre} → concierto inmersivo.` };
  }
  if (genre === 'jazz' || genre === 'r-and-b-soul' || genre === 'latin') {
    return { preset: 'concert', label: 'Club íntimo', palette: 'club', rationale: `Entorno local por género: ${genre} → club íntimo.` };
  }
  if (genre === 'classical' || genre === 'opera-vocal') {
    return { preset: 'nanotomix', label: 'Teatro / sala élite', palette: 'theatre', rationale: `Entorno local por género: ${genre} → teatro / sala élite.` };
  }
  if (genre === 'ambient-downtempo' || genre === 'world-folk') {
    return { preset: 'orbital', label: 'Campo espacial', palette: 'outdoor', rationale: `Entorno local por género: ${genre} → campo espacial.` };
  }
  return {
    preset: 'nanotomix',
    label: text.length >= 2 ? `Entorno: ${text}` : 'FotoHolo Nanotomix',
    palette: 'neutral',
    rationale: text.length >= 2
      ? `Entorno local: "${text}" sin arquetipo conocido → escena Nanotomix neutra.`
      : 'Entorno local: escena Nanotomix neutra.',
  };
}
