/**
 * IVANNA MEMORY LAYER v1.0
 * ─────────────────────────
 * Capa de memoria persistente del agente:
 * - Preferencias del usuario detectadas automáticamente
 * - Temas recurrentes
 * - Tono conversacional preferido
 * - Historial de sesión
 */

export interface UserPreferences {
  preferredLang: string;
  topicsDiscussed: string[];
  humorLevel: 'low' | 'medium' | 'high';
  technicality: 'casual' | 'mixed' | 'expert';
  lastActive: number;
  totalMessages: number;
  userName?: string;
}

export interface SessionMemory {
  sessionId: string;
  startTime: number;
  messageCount: number;
  dominantTopics: string[];
}

const STORAGE_KEY = 'ivanna_memory_v1';
const SESSION_KEY = 'ivanna_session_v1';

// ─── Topic detection patterns ─────────────────────────────────────────────────
const TOPIC_PATTERNS: Record<string, RegExp> = {
  audio_dsp:    /\b(dsp|eq|ecualizador|bass|treble|compresor|hrtf|audio|sonido|frecuencia|hz|db)\b/i,
  music:        /\b(música|canción|artista|álbum|banda|género|rock|jazz|reggaeton|pop|playlist)\b/i,
  technology:   /\b(tech|tecnología|programar|código|software|hardware|app|android|kernel|magisk)\b/i,
  science:      /\b(ciencia|física|química|biología|matemáticas|experimento|teoría|fórmula)\b/i,
  humor:        /\b(chiste|broma|gracioso|jaja|haha|lol|humor|chistoso|divertido)\b/i,
  philosophy:   /\b(filosofía|existencia|conciencia|moral|ética|sentido|vida|muerte|propósito)\b/i,
  cooking:      /\b(cocina|receta|comida|ingrediente|cocinar|sabor|platillo|chef)\b/i,
  sports:       /\b(deporte|fútbol|béisbol|baloncesto|equipo|partido|campeonato|atleta)\b/i,
};

// ─── Detección de nombre del usuario ──────────────────────────────────────────
const NAME_PATTERNS = [
  /me llamo\s+([A-Za-záéíóúÁÉÍÓÚñÑ]+)/i,
  /mi nombre es\s+([A-Za-záéíóúÁÉÍÓÚñÑ]+)/i,
  /soy\s+([A-Za-záéíóúÁÉÍÓÚñÑ]+)\s*[,\.]/i,
  /i'm\s+([A-Za-z]+)/i,
  /my name is\s+([A-Za-z]+)/i,
];

export class MemoryLayer {
  private prefs: UserPreferences;
  private session: SessionMemory;

  constructor() {
    this.prefs = this.loadPrefs();
    this.session = this.initSession();
  }

  // ── Carga preferencias persistentes ──────────────────────────────────────────
  private loadPrefs(): UserPreferences {
    try {
      const raw = localStorage.getItem(STORAGE_KEY);
      if (raw) return { ...this.defaultPrefs(), ...JSON.parse(raw) };
    } catch { /* storage no disponible */ }
    return this.defaultPrefs();
  }

  private defaultPrefs(): UserPreferences {
    return {
      preferredLang: 'es-MX',
      topicsDiscussed: [],
      humorLevel: 'medium',
      technicality: 'mixed',
      lastActive: Date.now(),
      totalMessages: 0,
    };
  }

  private savePrefs() {
    try {
      this.prefs.lastActive = Date.now();
      localStorage.setItem(STORAGE_KEY, JSON.stringify(this.prefs));
    } catch { /* storage no disponible */ }
  }

  // ── Session ──────────────────────────────────────────────────────────────────
  private initSession(): SessionMemory {
    try {
      const raw = sessionStorage.getItem(SESSION_KEY);
      if (raw) return JSON.parse(raw);
    } catch {}
    const session: SessionMemory = {
      sessionId: `s-${Date.now()}`,
      startTime: Date.now(),
      messageCount: 0,
      dominantTopics: [],
    };
    return session;
  }

  private saveSession() {
    try {
      sessionStorage.setItem(SESSION_KEY, JSON.stringify(this.session));
    } catch {}
  }

  // ── Analizar mensaje del usuario para extraer datos ───────────────────────────
  process(userText: string) {
    this.prefs.totalMessages++;
    this.session.messageCount++;

    // Detectar nombre
    if (!this.prefs.userName) {
      for (const pattern of NAME_PATTERNS) {
        const m = userText.match(pattern);
        if (m?.[1]) { this.prefs.userName = m[1]; break; }
      }
    }

    // Detectar topics
    for (const [topic, pattern] of Object.entries(TOPIC_PATTERNS)) {
      if (pattern.test(userText) && !this.prefs.topicsDiscussed.includes(topic)) {
        this.prefs.topicsDiscussed.push(topic);
        if (this.prefs.topicsDiscussed.length > 10) this.prefs.topicsDiscussed.shift();
      }
    }

    // Detectar nivel de humor
    const humorMatches = (userText.match(/\b(jaja|haha|chiste|broma|lol|xd)\b/gi) ?? []).length;
    if (humorMatches > 1) this.prefs.humorLevel = 'high';
    else if (humorMatches === 1 && this.prefs.humorLevel === 'low') this.prefs.humorLevel = 'medium';

    // Detectar nivel técnico
    const techMatches = (userText.match(/\b(dsp|hrtf|hz|db|fft|api|kernel|algorithm)\b/gi) ?? []).length;
    if (techMatches > 0) this.prefs.technicality = 'expert';

    this.savePrefs();
    this.saveSession();
  }

  // ── Genera contexto de memoria para el system prompt ─────────────────────────
  buildMemoryContext(): string {
    const parts: string[] = ['MEMORIA DEL USUARIO:'];

    if (this.prefs.userName) {
      parts.push(`El usuario se llama ${this.prefs.userName}. Úsalo ocasionalmente en la conversación.`);
    }

    if (this.prefs.topicsDiscussed.length > 0) {
      parts.push(`Temas que le interesan: ${this.prefs.topicsDiscussed.join(', ')}.`);
    }

    if (this.prefs.humorLevel === 'high') {
      parts.push('Le gusta mucho el humor. Puedes ser más bromista y gracioso con él.');
    } else if (this.prefs.humorLevel === 'low') {
      parts.push('Prefiere respuestas directas con poco humor.');
    }

    if (this.prefs.technicality === 'expert') {
      parts.push('Es un usuario técnico. Puedes usar terminología especializada de DSP y audio.');
    } else if (this.prefs.technicality === 'casual') {
      parts.push('Prefiere explicaciones accesibles sin mucho tecnicismo.');
    }

    if (this.prefs.totalMessages > 20) {
      parts.push(`Usuario frecuente (${this.prefs.totalMessages} mensajes históricos). Ya tiene confianza contigo.`);
    }

    return parts.length > 1 ? parts.join(' ') : '';
  }

  getPrefs(): Readonly<UserPreferences> { return this.prefs; }
  getSession(): Readonly<SessionMemory> { return this.session; }

  reset() {
    this.prefs = this.defaultPrefs();
    this.session = this.initSession();
    try { localStorage.removeItem(STORAGE_KEY); sessionStorage.removeItem(SESSION_KEY); } catch {}
  }
}
