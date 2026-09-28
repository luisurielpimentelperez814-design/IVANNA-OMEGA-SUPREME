/**
 * IVANNA VOICE INTELLIGENCE ENGINE v1.0
 * ──────────────────────────────────────
 * Sistema de voz ultra humana con:
 * - Adaptive Prosody System
 * - Emotion Detection Layer
 * - Context-Aware Speech Pipeline
 * - Micro-pause & rhythm engine
 */

export type EmotionClass =
  | 'neutral'
  | 'enthusiastic'
  | 'humor'
  | 'empathic'
  | 'technical'
  | 'contemplative'
  | 'assertive';

export interface ProsodyParams {
  rate: number;       // 0.1 – 2.0 (default 1.0)
  pitch: number;      // 0.0 – 2.0 (default 1.0)
  volume: number;     // 0.0 – 1.0
  pauseMs: number;    // pausa entre chunks (ms)
}

export interface SpeechChunk {
  text: string;
  prosody: ProsodyParams;
  emotion: EmotionClass;
}

// ─── Emotion keywords (español + inglés) ─────────────────────────────────────
const EMOTION_PATTERNS: Record<EmotionClass, RegExp> = {
  humor: /\b(jaja|jeje|haha|hehe|chiste|broma|gracioso|ironía|sarcasm|funny|lol|xd)\b/i,
  enthusiastic: /[!]{1,}|¡|(\b(increíble|wow|genial|fantástico|amazing|awesome|excelente|espectacular)\b)/i,
  empathic: /\b(entiendo|comprendo|siento|lamento|imagino|difícil|understand|sorry|feel)\b/i,
  technical: /\b(dsp|hrtf|hz|db|fft|compressor|equalizer|latency|buffer|sample|frequency|kernel|simd|neon)\b/i,
  contemplative: /\.{2,}|…|\b(quizás|tal vez|quizá|piénsalo|hmm|interesante|perhaps|maybe|wonder)\b/i,
  assertive: /\b(claramente|definitivamente|sin duda|absolutely|definitely|certainly|exactamente|exactly)\b/i,
  neutral: /./,
};

// ─── Base prosody por emoción ─────────────────────────────────────────────────
const EMOTION_PROSODY: Record<EmotionClass, ProsodyParams> = {
  neutral:       { rate: 0.87, pitch: 1.05, volume: 1.0, pauseMs: 90  },
  enthusiastic:  { rate: 0.94, pitch: 1.18, volume: 1.0, pauseMs: 60  },
  humor:         { rate: 0.92, pitch: 1.12, volume: 1.0, pauseMs: 70  },
  empathic:      { rate: 0.82, pitch: 1.02, volume: 0.95, pauseMs: 120 },
  technical:     { rate: 0.85, pitch: 1.00, volume: 1.0, pauseMs: 100 },
  contemplative: { rate: 0.78, pitch: 0.98, volume: 0.92, pauseMs: 160 },
  assertive:     { rate: 0.90, pitch: 1.08, volume: 1.0, pauseMs: 80  },
};

// ─── Detectar emoción dominante en un bloque de texto ────────────────────────
export function detectEmotion(text: string): EmotionClass {
  const scores: Partial<Record<EmotionClass, number>> = {};
  const lc = text.toLowerCase();

  for (const [emotion, pattern] of Object.entries(EMOTION_PATTERNS) as [EmotionClass, RegExp][]) {
    if (emotion === 'neutral') continue;
    const matches = lc.match(new RegExp(pattern.source, 'gi'));
    if (matches) scores[emotion] = (scores[emotion] ?? 0) + matches.length;
  }

  const sorted = (Object.entries(scores) as [EmotionClass, number][]).sort((a, b) => b[1] - a[1]);
  return sorted[0]?.[0] ?? 'neutral';
}

// ─── Chunker prosódico: divide el texto en unidades naturales ─────────────────
// Regla: punto/exclamación/interrogación = pausa larga, coma = pausa corta
function chunkText(text: string): string[] {
  // Primero dividir por oraciones
  const sentences = text
    .split(/(?<=[.!?¿¡])\s+/)
    .map(s => s.trim())
    .filter(Boolean);

  const chunks: string[] = [];

  for (const sentence of sentences) {
    // Dividir cláusulas largas por coma si superan 80 chars
    if (sentence.length > 80) {
      const clauses = sentence
        .split(/,\s+(?=[A-Za-záéíóúÁÉÍÓÚñÑ])/)
        .map(c => c.trim())
        .filter(Boolean);
      chunks.push(...clauses);
    } else {
      chunks.push(sentence);
    }
  }

  return chunks.length > 0 ? chunks : [text];
}

// ─── Limpiar texto para TTS: quitar markdown y caracteres disruptivos ─────────
export function cleanTextForTts(raw: string): string {
  return raw
    .replace(/\*\*(.+?)\*\*/g, '$1')      // bold
    .replace(/\*(.+?)\*/g, '$1')           // italic
    .replace(/`{1,3}(.+?)`{1,3}/gs, '$1') // code
    .replace(/#{1,6}\s/g, '')              // headings
    .replace(/\[(.+?)\]\(.+?\)/g, '$1')   // links
    .replace(/[-–•]\s+/g, ', ')           // bullets → natural list
    .replace(/\n{2,}/g, '. ')             // double newline → sentence end
    .replace(/\n/g, ', ')                 // single newline → pause
    .replace(/\s{2,}/g, ' ')              // extra spaces
    .trim();
}

// ─── Pipeline principal: texto → chunks con prosodia ─────────────────────────
export function buildSpeechPipeline(rawText: string): SpeechChunk[] {
  const cleaned = cleanTextForTts(rawText);
  const chunks = chunkText(cleaned);
  const globalEmotion = detectEmotion(cleaned);

  return chunks.map((text) => {
    // Emoción local del chunk puede variar del global
    const localEmotion = detectEmotion(text);
    const emotion: EmotionClass =
      localEmotion !== 'neutral' ? localEmotion : globalEmotion;

    const baseProsody = EMOTION_PROSODY[emotion];

    // Micro-ajuste por longitud del chunk: chunks cortos = ligeramente más lento
    const lengthFactor = text.length < 20 ? 0.95 : text.length > 100 ? 1.02 : 1.0;

    // Ajuste por signos de puntuación al final
    const endChar = text.slice(-1);
    const pitchBoost = endChar === '?' ? 1.08 : endChar === '!' ? 1.12 : 1.0;
    const rateAdjust = endChar === ',' ? 0.95 : 1.0;

    return {
      text: text.trim(),
      emotion,
      prosody: {
        rate: baseProsody.rate * lengthFactor * rateAdjust,
        pitch: baseProsody.pitch * pitchBoost,
        volume: baseProsody.volume,
        pauseMs: baseProsody.pauseMs,
      },
    };
  });
}

// ─── Voice Selector: busca la voz más natural disponible ─────────────────────
// Ordena por: neural > enhanced > local > cualquiera
export function selectBestVoice(
  voices: SpeechSynthesisVoice[],
  lang: string,
): SpeechSynthesisVoice | null {
  if (voices.length === 0) return null;

  const baseLang = lang.split('-')[0].toLowerCase();

  const priority: Array<(v: SpeechSynthesisVoice) => boolean> = [
    v => v.lang === lang && /neural|enhanced|premium/i.test(v.name),
    v => v.lang === lang && v.localService && !/male|masc/i.test(v.name),
    v => v.lang === lang && !/male|masc/i.test(v.name),
    v => v.lang === lang,
    v => v.lang.toLowerCase().startsWith(baseLang) && /neural|enhanced/i.test(v.name),
    v => v.lang.toLowerCase().startsWith(baseLang) && !/male|masc/i.test(v.name),
    v => v.lang.toLowerCase().startsWith(baseLang),
    // fallback premium en-US
    v => v.name === 'Samantha' || v.name === 'Karen' || v.name === 'Moira',
    v => v.lang === 'en-US' && v.localService,
    () => true,
  ];

  for (const pred of priority) {
    const found = voices.find(pred);
    if (found) return found;
  }
  return voices[0];
}

// ─── Context Memory: mantiene historial de emociones para coherencia ──────────
export class ConversationContext {
  private emotionHistory: EmotionClass[] = [];
  private readonly maxHistory = 5;

  track(emotion: EmotionClass) {
    this.emotionHistory.push(emotion);
    if (this.emotionHistory.length > this.maxHistory) {
      this.emotionHistory.shift();
    }
  }

  /** Devuelve la emoción dominante reciente para suavizar transiciones */
  dominantRecentEmotion(): EmotionClass {
    if (this.emotionHistory.length === 0) return 'neutral';
    const counts: Partial<Record<EmotionClass, number>> = {};
    for (const e of this.emotionHistory) {
      counts[e] = (counts[e] ?? 0) + 1;
    }
    const sorted = (Object.entries(counts) as [EmotionClass, number][])
      .sort((a, b) => b[1] - a[1]);
    return sorted[0][0];
  }

  /** Suaviza la transición entre la emoción actual y la dominante */
  blendProsody(current: ProsodyParams, blendFactor = 0.25): ProsodyParams {
    const dominant = EMOTION_PROSODY[this.dominantRecentEmotion()];
    return {
      rate:    current.rate    * (1 - blendFactor) + dominant.rate    * blendFactor,
      pitch:   current.pitch   * (1 - blendFactor) + dominant.pitch   * blendFactor,
      volume:  current.volume  * (1 - blendFactor) + dominant.volume  * blendFactor,
      pauseMs: Math.round(
                 current.pauseMs * (1 - blendFactor) + dominant.pauseMs * blendFactor
               ),
    };
  }

  reset() {
    this.emotionHistory = [];
  }
}
