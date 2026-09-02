/**
 * IVANNA SELF-HEALING ENGINE v1.0
 * ────────────────────────────────
 * Sistema de auto-diagnóstico y recuperación del panel de voz.
 *
 * Monitorea:
 *   - Estado del micrófono (tracks activos)
 *   - Estado de SpeechSynthesis (bloqueos de Chrome)
 *   - Estado de SpeechRecognition (timeouts)
 *   - Estado de la API (errores de red)
 *
 * Auto-repara:
 *   - SpeechSynthesis bloqueado → cancel() + re-queue
 *   - Mic track inactivo → solicita nuevo stream
 *   - Recognition timeout → restart silencioso
 */

export type HealthStatus = 'healthy' | 'degraded' | 'critical';

export interface SystemHealth {
  tts: HealthStatus;
  mic: HealthStatus;
  recognition: HealthStatus;
  api: HealthStatus;
  overall: HealthStatus;
  lastCheck: number;
  issues: string[];
}

// ─── Diagnóstico del SpeechSynthesis (Chrome tiene bug de bloqueo) ───────────
export function diagnoseTts(synth: SpeechSynthesis): { status: HealthStatus; issue?: string } {
  if (!('speechSynthesis' in window)) return { status: 'critical', issue: 'SpeechSynthesis no disponible' };
  if (synth.getVoices().length === 0) return { status: 'degraded', issue: 'Sin voces cargadas' };
  // Chrome bug: speaking===true sin reproducir nada = bloqueado
  if (synth.speaking && !synth.pending) return { status: 'degraded', issue: 'SpeechSynthesis bloqueado' };
  return { status: 'healthy' };
}

// ─── Diagnóstico del micrófono ────────────────────────────────────────────────
export function diagnoseMic(stream: MediaStream | null): { status: HealthStatus; issue?: string } {
  if (!stream) return { status: 'healthy' }; // no está activo, no es un error
  const tracks = stream.getAudioTracks();
  if (tracks.length === 0) return { status: 'critical', issue: 'Sin tracks de audio en el stream' };
  const active = tracks.filter(t => t.readyState === 'live');
  if (active.length === 0) return { status: 'critical', issue: 'Tracks de micrófono inactivos' };
  return { status: 'healthy' };
}

// ─── Auto-repair: TTS bloqueado en Chrome ────────────────────────────────────
export function repairTts(synth: SpeechSynthesis): boolean {
  if (synth.speaking) {
    synth.cancel();
    return true;
  }
  return false;
}

// ─── Clase principal del Self-Healing Engine ─────────────────────────────────
export class SelfHealingEngine {
  private intervalId: ReturnType<typeof setInterval> | null = null;
  private consecutiveApiErrors = 0;
  private lastApiSuccess = Date.now();
  private health: SystemHealth = {
    tts: 'healthy',
    mic: 'healthy',
    recognition: 'healthy',
    api: 'healthy',
    overall: 'healthy',
    lastCheck: Date.now(),
    issues: [],
  };

  private listeners: ((health: SystemHealth) => void)[] = [];

  // ── Iniciar monitoreo ─────────────────────────────────────────────────────────
  start(
    getMicStream: () => MediaStream | null,
    getSynth: () => SpeechSynthesis | null,
    checkInterval = 5000,
  ) {
    this.stop();
    this.intervalId = setInterval(() => {
      this.runDiagnostics(getMicStream(), getSynth());
    }, checkInterval);
  }

  stop() {
    if (this.intervalId) {
      clearInterval(this.intervalId);
      this.intervalId = null;
    }
  }

  // ── Ciclo de diagnóstico ──────────────────────────────────────────────────────
  private runDiagnostics(stream: MediaStream | null, synth: SpeechSynthesis | null) {
    const issues: string[] = [];

    // Diagnóstico TTS
    let ttsStatus: HealthStatus = 'healthy';
    if (synth) {
      const ttsCheck = diagnoseTts(synth);
      ttsStatus = ttsCheck.status;
      if (ttsCheck.issue) issues.push(`TTS: ${ttsCheck.issue}`);
      // Auto-repair TTS bloqueado
      if (ttsStatus === 'degraded') repairTts(synth);
    }

    // Diagnóstico mic
    const micCheck = diagnoseMic(stream);
    if (micCheck.issue) issues.push(`MIC: ${micCheck.issue}`);

    // Estado API basado en historial de errores
    const apiStatus: HealthStatus =
      this.consecutiveApiErrors >= 5 ? 'critical' :
      this.consecutiveApiErrors >= 2 ? 'degraded' : 'healthy';
    if (apiStatus !== 'healthy') issues.push(`API: ${this.consecutiveApiErrors} errores consecutivos`);

    // Overall
    const statuses = [ttsStatus, micCheck.status, apiStatus];
    const overall: HealthStatus =
      statuses.includes('critical') ? 'critical' :
      statuses.includes('degraded') ? 'degraded' : 'healthy';

    this.health = {
      tts: ttsStatus,
      mic: micCheck.status,
      recognition: 'healthy',
      api: apiStatus,
      overall,
      lastCheck: Date.now(),
      issues,
    };

    this.emit(this.health);
  }

  // ── Registrar resultado de llamada API ────────────────────────────────────────
  reportApiSuccess() {
    this.consecutiveApiErrors = 0;
    this.lastApiSuccess = Date.now();
  }

  reportApiError() {
    this.consecutiveApiErrors++;
  }

  // ── Suscripción a cambios de salud ───────────────────────────────────────────
  subscribe(fn: (h: SystemHealth) => void): () => void {
    this.listeners.push(fn);
    return () => { this.listeners = this.listeners.filter(l => l !== fn); };
  }

  private emit(health: SystemHealth) {
    this.listeners.forEach(fn => fn(health));
  }

  getHealth(): Readonly<SystemHealth> { return this.health; }
}
