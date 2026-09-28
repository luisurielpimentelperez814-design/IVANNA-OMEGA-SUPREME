/**
 * IVANNA AUDIO KNOWLEDGE CORE v1.0
 * ──────────────────────────────────
 * Base de conocimiento especializada en DSP y audio.
 * Proporciona contexto técnico dinámico al agente IA.
 */

import type { DspParameters } from '../types';

// ─── Estado del DSP traducido a lenguaje natural ──────────────────────────────
export function describeDspState(params: DspParameters): string {
  const lines: string[] = [];

  // Master
  lines.push(`Ganancia maestra al ${Math.round(params.masterGain * 100)}%. Motor DSP ${params.masterBypass ? 'en BYPASS' : 'activo'}.`);

  // Anti-Dolby
  lines.push(`Anti-Dolby al ${Math.round(params.antiDolbyIntensity * 100)}% — ${
    params.antiDolbyIntensity > 0.85 ? 'modo agresivo (anti-compresión máxima)' :
    params.antiDolbyIntensity > 0.6  ? 'modo moderado' : 'modo suave'
  }.`);

  // Golden Ear
  if (params.goldenEarEnabled) {
    lines.push(`Golden Ear GAN activo — Drive ${params.goldenEarDrive.toFixed(2)}x, Mix ${Math.round(params.goldenEarMix * 100)}% (excitación armónica psicoacústica).`);
  } else {
    lines.push('Golden Ear GAN desactivado.');
  }

  // HRTF Espacial
  if (params.hrtfEnabled) {
    lines.push(`HRTF 3D activo — ángulo ${params.spatialAngleDeg}°, ancho ${params.spatialWidth.toFixed(2)}, delay ${params.hrtfDelayMs}ms.`);
  }

  // Compresor
  lines.push(`Compresor: umbral ${params.compThresholdDb}dBFS, ratio ${params.compRatio}:1, attack ${params.compAttackMs}ms, release ${params.compReleaseMs}ms.`);

  // EQ evolutiva
  lines.push(`EQ evolutiva CMA-ES: tasa de mutación ${params.eqMutationRate.toFixed(2)}.`);

  // Config de bloque
  lines.push(`Bloque de audio: ${params.blockSize} muestras a ${params.sampleRate}Hz (${((params.blockSize / params.sampleRate) * 1000).toFixed(2)}ms de latencia teórica).`);

  // Preset
  lines.push(`Preset activo: ${params.activePreset}.`);

  return lines.join(' ');
}

// ─── Sistema de conocimiento para el agente ──────────────────────────────────
export const AUDIO_KNOWLEDGE = `
PARÁMETROS DSP Y SU SIGNIFICADO PARA EL AGENTE:

masterGain: Ganancia de salida principal. 1.0 = 0dBFS. >1.15 riesgo de clip.
antiDolbyIntensity: Fuerza del filtro anti-compresión Dolby. 0.85+ = modo extremo.
goldenEarEnabled / goldenEarDrive / goldenEarMix: Excitador armónico basado en GAN. Añade armónicos pares/impares para percepción de calidez. Drive 1.0-2.5.
hrtfEnabled / hrtfDelayMs / spatialAngleDeg / spatialWidth: Procesado HRTF 3D. Simula audición binaural con Head-Related Transfer Function. Angulo 0-90°, width 0.8-1.8.
eqMutationRate: Velocidad de evolución del EQ CMA-ES (CMA = Covariance Matrix Adaptation). Mayor tasa = adaptación más agresiva.
compThresholdDb / compRatio / compAttackMs / compReleaseMs: Compresor dinámico. Protege clipping y controla dinámica.
fatigueIndex: Índice de fatiga auditiva (IIR low-pass). Mayor = mayor protección en escucha larga.
blockSize / sampleRate: Tamaño de buffer y frecuencia de muestreo. 512 @ 48kHz = 10.67ms de latencia de bloque.
nhoAlpha / nhoBeta: Natural Harmonic Oscillator — síntesis de sub-armónicos y armónicos superiores.
harmonicGain: Ganancia de la etapa de enriquecimiento armónico.

PSICOACÚSTICA RELEVANTE:
- Efecto Stevens: percepción de loudness es potencial (ley potencia, no lineal).
- Efecto Haas: retrasos <35ms se perciben como dirección, no eco.
- Curvas ISO 226: loudness igual percibido varía por frecuencia (lo grave necesita más dB).
- HRTF individual: varía por anatomía del pabellón auricular. Generalizado = 70-80% efectivo.
- Armónicos pares (2°, 4°): asociados a calidez analógica. Impares (3°, 5°): distorsión percibida.
`;

// ─── Genera contexto dinámico para el system prompt ──────────────────────────
export function buildAgentContext(params?: DspParameters): string {
  let ctx = AUDIO_KNOWLEDGE;
  if (params) {
    ctx += '\n\nESTADO DSP ACTUAL DEL MOTOR:\n' + describeDspState(params);
  }
  return ctx;
}
