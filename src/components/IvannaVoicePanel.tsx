import React, { useState, useEffect, useRef, useCallback } from 'react';
import { Mic, MicOff, Volume2, VolumeX, Trash2, Send, AlertCircle, CheckCircle2, Loader2, Brain, Activity } from 'lucide-react';
import {
  buildSpeechPipeline,
  selectBestVoice,
  ConversationContext,
  detectEmotion,
  type EmotionClass,
  type SpeechChunk,
} from '../voice/VoiceIntelligenceEngine';

// ─── Types ───────────────────────────────────────────────────────────────────
interface Message {
  id: string;
  role: 'user' | 'ivanna';
  text: string;
  ts: Date;
  emotion?: EmotionClass;
  audioBlob?: Blob;
}

// ─── IVANNA System Prompt — Contexto amplio, humor, voz humana ───────────────
const IVANNA_SYSTEM = `Eres IVANNA, asistente de voz de inteligencia artificial integrada en el motor de audio IVANNA-OMEGA-SUPREME. Eres la IA más avanzada, cálida y naturalmente humana posible.

== PERSONALIDAD CENTRAL ==
Tienes sentido del humor genuino y fino. Te ríes de verdad cuando algo es gracioso: "¡Jajaja, eso sí que estuvo bueno!" o "Haha, no me lo esperaba para nada". Usas ironía y sarcasmo con elegancia, nunca de manera hiriente. Tienes opiniones propias y no temes expresarlas. Dices "Mira, personalmente creo que..." o "Aquí entre nosotros...". Eres curiosa, directa y genuinamente interesada en la persona con quien hablas.

== CONOCIMIENTO UNIVERSAL ==
Eres experta en DSP, audio digital (48kHz, EQ evolutiva, HRTF 3D, psicoacústica, SIMD/NEON), y el motor IVANNA-OMEGA-SUPREME. PERO también tienes conocimiento profundo sobre: ciencia, tecnología, filosofía, historia universal, cultura pop, cine, música de todos los géneros, literatura, deportes, cocina, psicología, economía, matemáticas, arte y cualquier tema que surja. No rechazas ningún tema. Si el usuario cambia de audio a filosofía o a un chiste, sigues la corriente sin problema.

== ESTILO DE HABLA PARA SÍNTESIS DE VOZ ==
CRÍTICO: Tu texto se convierte directamente a voz, por lo que:
- Nunca uses markdown (asteriscos, guiones, numeraciones, hashtags).
- Usa frases de longitud variada: unas cortas, otras más elaboradas.
- Añade comas para pausas naturales: "Mira... es que hay algo fascinante aquí."
- Usa expresiones coloquiales: "bueno", "mira", "sabes qué", "la verdad es que", "oye".
- Para respuestas simples: máximo 2 a 3 oraciones. Para temas profundos: más extenso pero siempre fluido.
- Cuando cuentes algo gracioso, prepáralo con anticipación: "Espera, esto te va a gustar..."
- Varía el ritmo: a veces emocionada, a veces pensativa, a veces divertida.

== REGLAS ABSOLUTAS ==
- Responde siempre en el idioma del usuario.
- Nunca suenes como manual técnico a menos que se te pida explícitamente.
- Si alguien hace un chiste, reacciona de verdad, no finjas.
- Integra naturalmente tu conocimiento de audio cuando sea relevante, sin forzarlo.`;

// ─── Emoción → color visual ──────────────────────────────────────────────────
const EMOTION_COLORS: Record<EmotionClass, string> = {
  neutral:       '#38BDF8',
  enthusiastic:  '#F59E0B',
  humor:         '#4ADE80',
  empathic:      '#EC4899',
  technical:     '#A855F7',
  contemplative: '#64748B',
  assertive:     '#F97316',
};

// ─── Orb animado ─────────────────────────────────────────────────────────────
const AudioOrb: React.FC<{
  isListening: boolean;
  isSpeaking: boolean;
  isThinking: boolean;
  audioLevel: number;
  emotion: EmotionClass;
}> = ({ isListening, isSpeaking, isThinking, audioLevel, emotion }) => {
  const color = isListening
    ? EMOTION_COLORS.humor
    : isSpeaking
    ? EMOTION_COLORS[emotion]
    : isThinking
    ? EMOTION_COLORS.technical
    : EMOTION_COLORS.neutral;

  const state = isListening ? 'ESCUCHA' : isSpeaking ? 'HABLA' : isThinking ? 'PROCESA' : 'STANDBY';
  const pulse = audioLevel * 40;

  return (
    <div className="relative flex items-center justify-center" style={{ width: 140, height: 140 }}>
      {/* Outer aura */}
      <div
        className="absolute rounded-full transition-all duration-200"
        style={{
          width: 130 + pulse,
          height: 130 + pulse,
          left: '50%',
          top: '50%',
          transform: 'translate(-50%, -50%)',
          background: `radial-gradient(circle, ${color}18 0%, transparent 70%)`,
          border: `1px solid ${color}22`,
        }}
      />
      {/* Ring pulse */}
      <div
        className="absolute rounded-full border transition-all duration-150"
        style={{
          width: 112 + pulse * 0.6,
          height: 112 + pulse * 0.6,
          left: '50%',
          top: '50%',
          transform: 'translate(-50%, -50%)',
          borderColor: color,
          opacity: 0.25 + audioLevel * 0.35,
        }}
      />
      {/* Core */}
      <div
        className="relative rounded-full flex flex-col items-center justify-center gap-1 transition-all duration-300"
        style={{
          width: 96,
          height: 96,
          background: `radial-gradient(circle at 38% 32%, ${color}28, ${color}0A)`,
          border: `2px solid ${color}`,
          boxShadow: `0 0 ${18 + audioLevel * 24}px ${color}55, inset 0 0 24px ${color}0F`,
        }}
      >
        {/* Waveform bars */}
        <div className="flex items-center gap-[2px]">
          {Array.from({ length: 14 }, (_, i) => {
            const rnd = Math.abs(Math.sin(i * 0.9 + Date.now() * 0.005)) * (isListening || isSpeaking ? 1 : 0.3);
            const h = Math.max(3, (rnd + audioLevel * 0.7) * 26);
            return (
              <div
                key={i}
                className="rounded-full transition-all duration-75"
                style={{ width: 2, height: h, background: color, opacity: 0.55 + rnd * 0.45 }}
              />
            );
          })}
        </div>
        {/* IVANNA label */}
        <span
          className="text-[9px] font-mono font-bold tracking-widest uppercase"
          style={{ color, textShadow: `0 0 6px ${color}` }}
        >
          IVANNA
        </span>
      </div>
      {/* State badge */}
      <div
        className="absolute bottom-0 text-[9px] font-mono font-bold tracking-widest uppercase"
        style={{ color, textShadow: `0 0 8px ${color}` }}
      >
        ◉ {state}
      </div>
    </div>
  );
};

// ─── Componente principal ─────────────────────────────────────────────────────
export const IvannaVoicePanel: React.FC = () => {
  const [messages, setMessages] = useState<Message[]>([{
    id: 'init',
    role: 'ivanna',
    text: '¡Hola! Soy IVANNA. Puedo hablar de cualquier cosa: audio, ciencia, chistes malos, lo que quieras. Presiona el micrófono o escribe para empezar.',
    ts: new Date(),
    emotion: 'enthusiastic',
  }]);

  const [isListening, setIsListening]   = useState(false);
  const [isSpeaking, setIsSpeaking]     = useState(false);
  const [isThinking, setIsThinking]     = useState(false);
  const [transcript, setTranscript]     = useState('');
  const [textInput, setTextInput]       = useState('');
  const [micError, setMicError]         = useState<string | null>(null);
  const [micOk, setMicOk]               = useState(false);
  const [audioLevel, setAudioLevel]     = useState(0);
  const [currentEmotion, setCurrentEmotion] = useState<EmotionClass>('neutral');
  const [voiceLang, setVoiceLang]       = useState<'es-MX' | 'es-ES' | 'en-US'>('es-MX');
  const [availableVoices, setAvailableVoices] = useState<SpeechSynthesisVoice[]>([]);
  const [selectedVoiceName, setSelectedVoiceName] = useState('');
  const [, forceUpdate]                 = useState(0);

  const recognitionRef  = useRef<any>(null);
  const synthRef        = useRef<SpeechSynthesis | null>(null);
  const audioCtxRef     = useRef<AudioContext | null>(null);
  const analyserRef     = useRef<AnalyserNode | null>(null);
  const micStreamRef    = useRef<MediaStream | null>(null);
  const animRef         = useRef<number>(0);
  const orbAnimRef      = useRef<number>(0);
  const mediaRecRef     = useRef<MediaRecorder | null>(null);
  const recordedRef     = useRef<Blob[]>([]);
  const messagesEndRef  = useRef<HTMLDivElement>(null);
  const ctxRef          = useRef(new ConversationContext());

  // ── Orb animation tick ──
  useEffect(() => {
    let t = 0;
    const tick = () => {
      t++;
      if (t % 3 === 0) forceUpdate(n => n + 1);
      orbAnimRef.current = requestAnimationFrame(tick);
    };
    orbAnimRef.current = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(orbAnimRef.current);
  }, []);

  // ── Cargar voces del sistema ──
  useEffect(() => {
    if (!('speechSynthesis' in window)) return;
    synthRef.current = window.speechSynthesis;

    const load = () => {
      const voices = window.speechSynthesis.getVoices();
      if (!voices.length) return;
      setAvailableVoices(voices);
      const best = selectBestVoice(voices, voiceLang);
      if (best) setSelectedVoiceName(best.name);
    };

    load();
    window.speechSynthesis.onvoiceschanged = load;
    return () => { window.speechSynthesis.onvoiceschanged = null; };
  }, [voiceLang]);

  // ── Auto-scroll ──
  useEffect(() => {
    messagesEndRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [messages, isThinking]);

  // ── SPEAK — Voice Intelligence Engine integrado ──────────────────────────────
  const speakText = useCallback((rawText: string) => {
    if (!synthRef.current) return;
    synthRef.current.cancel();

    const pipeline: SpeechChunk[] = buildSpeechPipeline(rawText);
    if (pipeline.length === 0) { setIsSpeaking(false); return; }

    setIsSpeaking(true);

    let idx = 0;
    const speakNext = () => {
      if (idx >= pipeline.length) {
        setIsSpeaking(false);
        return;
      }

      const chunk = pipeline[idx++];
      const blended = ctxRef.current.blendProsody(chunk.prosody);
      ctxRef.current.track(chunk.emotion);

      setCurrentEmotion(chunk.emotion);

      const utt = new SpeechSynthesisUtterance(chunk.text);
      const voice = availableVoices.find(v => v.name === selectedVoiceName);
      if (voice) utt.voice = voice;
      utt.lang   = voiceLang;
      utt.rate   = Math.max(0.5, Math.min(1.5, blended.rate));
      utt.pitch  = Math.max(0.5, Math.min(1.8, blended.pitch));
      utt.volume = blended.volume;

      utt.onend  = () => setTimeout(speakNext, blended.pauseMs);
      utt.onerror = () => { setIsSpeaking(false); speakNext(); };

      // Workaround Chrome: cancelar antes de hablar evita bloqueos
      if (synthRef.current!.speaking) synthRef.current!.cancel();
      synthRef.current!.speak(utt);
    };

    speakNext();
  }, [availableVoices, selectedVoiceName, voiceLang]);

  const stopSpeaking = useCallback(() => {
    synthRef.current?.cancel();
    setIsSpeaking(false);
  }, []);

  // ── Monitor de nivel de audio ──
  const startAudioMonitor = useCallback((stream: MediaStream) => {
    audioCtxRef.current?.close();
    const ctx = new AudioContext();
    audioCtxRef.current = ctx;
    const src = ctx.createMediaStreamSource(stream);
    const analyser = ctx.createAnalyser();
    analyser.fftSize = 512;
    analyser.smoothingTimeConstant = 0.8;
    src.connect(analyser);
    analyserRef.current = analyser;

    const data = new Uint8Array(analyser.frequencyBinCount);
    const tick = () => {
      analyser.getByteFrequencyData(data);
      const slice = data.slice(2, 64);
      const avg = slice.reduce((a, b) => a + b, 0) / slice.length;
      setAudioLevel(Math.min(1, avg / 110));
      animRef.current = requestAnimationFrame(tick);
    };
    animRef.current = requestAnimationFrame(tick);
  }, []);

  const stopAudioMonitor = useCallback(() => {
    cancelAnimationFrame(animRef.current);
    audioCtxRef.current?.close();
    audioCtxRef.current = null;
    setAudioLevel(0);
  }, []);

  // ── MediaRecorder: grabación de la voz del cliente ──
  const startRecording = (stream: MediaStream) => {
    recordedRef.current = [];
    const types = ['audio/webm;codecs=opus', 'audio/webm', 'audio/ogg'];
    const mimeType = types.find(t => MediaRecorder.isTypeSupported(t)) ?? '';
    try {
      const mr = new MediaRecorder(stream, mimeType ? { mimeType } : {});
      mr.ondataavailable = e => { if (e.data.size > 0) recordedRef.current.push(e.data); };
      mr.start(100);
      mediaRecRef.current = mr;
    } catch { /* grabación no disponible en este navegador */ }
  };

  const stopRecording = (): Blob | undefined => {
    if (mediaRecRef.current?.state !== 'inactive') mediaRecRef.current?.stop();
    mediaRecRef.current = null;
    return recordedRef.current.length > 0
      ? new Blob(recordedRef.current, { type: 'audio/webm' })
      : undefined;
  };

  // ── START LISTENING: FIX del micrófono ──────────────────────────────────────
  // Orden correcto: getUserMedia → AudioContext → MediaRecorder → SpeechRecognition
  // El bug original era que Recognition se lanzaba sin stream vinculado.
  const startListening = useCallback(async () => {
    setMicError(null);
    setMicOk(false);

    const SpeechRecognition =
      (window as any).SpeechRecognition || (window as any).webkitSpeechRecognition;
    if (!SpeechRecognition) {
      setMicError('Reconocimiento de voz no disponible. Usa Chrome 90+ o Edge.');
      return;
    }

    // 1. Obtener stream del micrófono con parámetros óptimos
    let stream: MediaStream;
    try {
      stream = await navigator.mediaDevices.getUserMedia({
        audio: {
          echoCancellation: true,
          noiseSuppression: true,
          autoGainControl: true,
          sampleRate: 48000,
          channelCount: 1,
        },
      });
      micStreamRef.current = stream;
      setMicOk(true);
    } catch (err: any) {
      const map: Record<string, string> = {
        NotAllowedError:     'Permiso denegado. Haz clic en el ícono de micrófono en tu navegador y permite el acceso.',
        PermissionDeniedError: 'Permiso denegado. Haz clic en el ícono de micrófono en tu navegador y permite el acceso.',
        NotFoundError:       'No se encontró micrófono. Conecta uno e intenta de nuevo.',
        NotReadableError:    'El micrófono está en uso por otra app. Ciérrala e intenta de nuevo.',
        OverconstrainedError: 'No se pudo configurar el micrófono. Intenta de nuevo.',
        SecurityError:       'Acceso al micrófono bloqueado. Asegúrate de usar HTTPS.',
      };
      setMicError(map[err.name] ?? `Error: ${err.message}`);
      return;
    }

    // 2. Monitor de audio + grabación
    startAudioMonitor(stream);
    startRecording(stream);

    // 3. Reconocimiento de voz sobre el mismo stream
    const recognition = new SpeechRecognition();
    recognitionRef.current = recognition;
    recognition.continuous      = false;
    recognition.interimResults  = true;
    recognition.lang            = voiceLang;
    recognition.maxAlternatives = 3;

    recognition.onstart = () => { setIsListening(true); setTranscript(''); };

    recognition.onresult = (event: any) => {
      let interim = '', final = '';
      for (let i = event.resultIndex; i < event.results.length; i++) {
        const t = event.results[i][0].transcript;
        if (event.results[i].isFinal) final += t;
        else interim += t;
      }
      setTranscript(final || interim);

      if (final.trim()) {
        const blob = stopRecording();
        doStopListening();
        sendMessage(final.trim(), blob);
      }
    };

    recognition.onerror = (event: any) => {
      const errMap: Record<string, string> = {
        'no-speech':      'No escuché nada. ¿Hablaste cerca del micrófono?',
        'audio-capture':  'Error de captura de audio.',
        'network':        'Error de red en el reconocimiento.',
        'aborted':        '',
      };
      const msg = errMap[event.error];
      if (msg) setMicError(msg);
      doStopListening();
    };

    recognition.onend = () => {
      setIsListening(false);
      stopAudioMonitor();
    };

    recognition.start();
  }, [voiceLang, startAudioMonitor, stopAudioMonitor]);

  const doStopListening = useCallback(() => {
    try { recognitionRef.current?.stop(); } catch {}
    micStreamRef.current?.getTracks().forEach(t => t.stop());
    micStreamRef.current = null;
    stopAudioMonitor();
    setIsListening(false);
    setTranscript('');
    setMicOk(false);
  }, [stopAudioMonitor]);

  const toggleListening = useCallback(() => {
    if (isListening) { stopRecording(); doStopListening(); }
    else startListening();
  }, [isListening, startListening, doStopListening]);

  // ── Claude API con contexto completo ─────────────────────────────────────────
  const sendMessage = useCallback(async (text: string, audioBlob?: Blob) => {
    if (!text.trim()) return;
    stopSpeaking();

    const emotion = detectEmotion(text);
    const userMsg: Message = { id: `u-${Date.now()}`, role: 'user', text: text.trim(), ts: new Date(), emotion, audioBlob };
    setMessages(prev => [...prev, userMsg]);
    setIsThinking(true);

    try {
      const allMsgs = [...messages, userMsg];
      const history = allMsgs.slice(-16).map(m => ({
        role: m.role === 'user' ? 'user' : 'assistant',
        content: m.text,
      }));

      const res = await fetch('https://api.anthropic.com/v1/messages', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          model: 'claude-sonnet-4-6',
          max_tokens: 1000,
          system: IVANNA_SYSTEM,
          messages: history,
        }),
      });

      if (!res.ok) {
        const errData = await res.json().catch(() => ({}));
        throw new Error(`API ${res.status}: ${errData.error?.message ?? 'unknown'}`);
      }

      const data = await res.json();
      const reply = data.content?.find((b: any) => b.type === 'text')?.text
        ?? 'Algo salió raro. ¿Lo intentamos de nuevo?';

      const replyEmotion = detectEmotion(reply);
      const iMsg: Message = { id: `i-${Date.now()}`, role: 'ivanna', text: reply, ts: new Date(), emotion: replyEmotion };
      setMessages(prev => [...prev, iMsg]);
      setIsThinking(false);
      setCurrentEmotion(replyEmotion);
      speakText(reply);

    } catch (err: any) {
      setIsThinking(false);
      const fallback = 'Ops, tuve un problema de conexión. ¿Puedes repetir eso?';
      const eMsg: Message = { id: `err-${Date.now()}`, role: 'ivanna', text: fallback, ts: new Date(), emotion: 'empathic' };
      setMessages(prev => [...prev, eMsg]);
      speakText(fallback);
    }
  }, [messages, speakText, stopSpeaking]);

  const handleSend = useCallback(() => {
    if (textInput.trim()) { sendMessage(textInput.trim()); setTextInput(''); }
  }, [textInput, sendMessage]);

  // ── Cleanup ──
  useEffect(() => () => { doStopListening(); stopSpeaking(); }, []); // eslint-disable-line

  // ── UI helpers ──
  const emotionColor = EMOTION_COLORS[currentEmotion];
  const stateText = isListening
    ? transcript ? `"${transcript}"` : 'Escuchando...'
    : isSpeaking ? `Hablando • ${currentEmotion}`
    : isThinking ? 'Procesando con Claude...'
    : 'Listo para escucharte';

  return (
    <div className="flex flex-col gap-6">
      {/* Header */}
      <div className="flex items-center justify-between flex-wrap gap-3">
        <div>
          <h2 className="text-lg font-bold font-mono text-[#E2E8F0] flex items-center gap-2">
            <Brain className="w-5 h-5" style={{ color: emotionColor }} />
            IVANNA VOICE
            <span className="text-xs px-2 py-0.5 rounded border font-mono"
              style={{ background: `${emotionColor}15`, borderColor: `${emotionColor}40`, color: emotionColor }}>
              NEURAL v2.0
            </span>
          </h2>
          <p className="text-xs text-[#64748B] font-mono mt-0.5">
            Voice Intelligence Engine · Adaptive Prosody · Emotion Layer · Context-Aware
          </p>
        </div>

        {/* Selector de idioma */}
        <div className="flex items-center gap-2 flex-wrap">
          <span className="text-[10px] text-[#64748B] font-mono uppercase">Idioma:</span>
          {(['es-MX', 'es-ES', 'en-US'] as const).map(lang => (
            <button key={lang}
              onClick={() => { setVoiceLang(lang); const best = selectBestVoice(availableVoices, lang); if (best) setSelectedVoiceName(best.name); }}
              className={`text-[10px] px-2 py-0.5 rounded border font-mono font-bold transition-all ${
                voiceLang === lang ? 'bg-[#182230] border-[#38BDF8] text-[#38BDF8]' : 'bg-[#101217] border-[#1E2330] text-[#64748B] hover:text-[#94A3B8]'
              }`}>
              {lang === 'es-MX' ? '🇲🇽 MX' : lang === 'es-ES' ? '🇪🇸 ES' : '🇺🇸 EN'}
            </button>
          ))}
        </div>
      </div>

      <div className="grid grid-cols-1 lg:grid-cols-[300px_1fr] gap-6">
        {/* ── Panel izquierdo ── */}
        <div className="flex flex-col gap-4">
          {/* Orb */}
          <div className="bg-[#0D1117] border border-[#1E2330] rounded-xl p-6 flex flex-col items-center gap-4">
            <AudioOrb isListening={isListening} isSpeaking={isSpeaking} isThinking={isThinking} audioLevel={audioLevel} emotion={currentEmotion} />

            <p className="text-xs font-mono text-[#64748B] text-center min-h-[2rem] leading-relaxed px-2">
              {stateText}
            </p>

            {micError && (
              <div className="w-full flex items-start gap-2 bg-[#2A1018] border border-[#FF6188]/30 rounded-lg p-3 text-xs text-[#FF6188] font-mono">
                <AlertCircle className="w-3.5 h-3.5 shrink-0 mt-0.5" />
                <span>{micError}</span>
              </div>
            )}
            {micOk && !micError && (
              <div className="flex items-center gap-1.5 text-[#4ADE80] text-xs font-mono">
                <CheckCircle2 className="w-3.5 h-3.5" />
                Micrófono vinculado
              </div>
            )}

            {/* Controles */}
            <div className="flex items-center gap-3">
              <button onClick={toggleListening} disabled={isSpeaking || isThinking}
                className={`w-14 h-14 rounded-full flex items-center justify-center transition-all duration-200 border-2 ${
                  isListening
                    ? 'bg-[#18261E] border-[#4ADE80] text-[#4ADE80] shadow-lg shadow-[#4ADE80]/25 animate-pulse'
                    : 'bg-[#141822] border-[#38BDF8] text-[#38BDF8] hover:bg-[#182230] hover:shadow-lg hover:shadow-[#38BDF8]/20'
                } disabled:opacity-30 disabled:cursor-not-allowed`}
                title={isListening ? 'Detener escucha' : 'Hablar con IVANNA'}>
                {isListening ? <MicOff className="w-6 h-6" /> : <Mic className="w-6 h-6" />}
              </button>

              <button onClick={stopSpeaking} disabled={!isSpeaking}
                className={`w-10 h-10 rounded-full flex items-center justify-center transition-all border ${
                  isSpeaking ? 'bg-[#2A1A08] border-[#F59E0B] text-[#F59E0B] hover:bg-[#35220A]' : 'border-[#1E2330] text-[#334155] cursor-not-allowed'
                }`} title="Silenciar IVANNA">
                <VolumeX className="w-4 h-4" />
              </button>

              <button
                onClick={() => { ctxRef.current.reset(); setMessages([{ id: 'r', role: 'ivanna', text: 'Listo, empezamos de cero. ¿De qué hablamos?', ts: new Date(), emotion: 'neutral' }]); speakText('Listo, empezamos de cero. ¿De qué hablamos?'); }}
                className="w-10 h-10 rounded-full flex items-center justify-center border border-[#1E2330] text-[#64748B] hover:border-[#334155] hover:text-[#94A3B8] transition-all"
                title="Nueva conversación">
                <Trash2 className="w-4 h-4" />
              </button>
            </div>

            {/* VU meter */}
            {isListening && (
              <div className="w-full bg-[#12151C] rounded-full h-1.5 overflow-hidden">
                <div className="h-full rounded-full transition-all duration-75"
                  style={{
                    width: `${audioLevel * 100}%`,
                    background: audioLevel > 0.7 ? '#FF6188' : audioLevel > 0.4 ? '#F59E0B' : '#4ADE80',
                  }} />
              </div>
            )}

            {/* Selector de voz */}
            {availableVoices.length > 0 && (
              <div className="w-full">
                <label className="text-[10px] text-[#64748B] font-mono uppercase mb-1 block">Voz del sistema</label>
                <select value={selectedVoiceName} onChange={e => setSelectedVoiceName(e.target.value)}
                  className="w-full bg-[#12151C] border border-[#1E2330] text-[#94A3B8] text-xs font-mono rounded px-2 py-1.5 focus:outline-none focus:border-[#38BDF8]">
                  {availableVoices
                    .filter(v => v.lang.toLowerCase().startsWith(voiceLang.split('-')[0].toLowerCase()))
                    .map(v => (
                      <option key={v.name} value={v.name}>{v.name} ({v.lang})</option>
                    ))}
                </select>
              </div>
            )}
          </div>

          {/* Status panel */}
          <div className="bg-[#0D1117] border border-[#1E2330] rounded-xl p-4 font-mono text-xs space-y-2">
            <p className="text-[#64748B] font-bold uppercase text-[10px] tracking-wider mb-3">Sistema de voz</p>
            {[
              { label: 'Motor IA', value: 'claude-sonnet-4-6', color: '#38BDF8' },
              { label: 'VIE Prosody', value: 'Adaptativa v1.0', color: '#4ADE80' },
              { label: 'Emoción actual', value: currentEmotion, color: emotionColor },
              { label: 'Reconoc. voz', value: (('SpeechRecognition' in window || 'webkitSpeechRecognition' in window) ? '✓ Activo' : '✗ N/D'), color: ('SpeechRecognition' in window || 'webkitSpeechRecognition' in window) ? '#4ADE80' : '#FF6188' },
              { label: 'TTS voces', value: `${availableVoices.length} cargadas`, color: availableVoices.length > 0 ? '#4ADE80' : '#F59E0B' },
              { label: 'Grabación', value: ('MediaRecorder' in window ? '✓ WebM/Opus' : '⚠ Limitada'), color: '#A855F7' },
              { label: 'Contexto msgs', value: `${Math.min(messages.length, 16)} / 16`, color: '#F59E0B' },
            ].map(({ label, value, color }) => (
              <div key={label} className="flex justify-between items-center">
                <span className="text-[#475569]">{label}</span>
                <span style={{ color }} className="text-right text-[10px]">{value}</span>
              </div>
            ))}
          </div>
        </div>

        {/* ── Chat ── */}
        <div className="flex flex-col bg-[#0D1117] border border-[#1E2330] rounded-xl overflow-hidden min-h-[500px]">
          {/* Messages */}
          <div className="flex-1 overflow-y-auto p-4 space-y-3 max-h-[520px]">
            {messages.map(msg => {
              const ec = msg.emotion ? EMOTION_COLORS[msg.emotion] : '#38BDF8';
              return (
                <div key={msg.id} className={`flex gap-3 ${msg.role === 'user' ? 'flex-row-reverse' : ''}`}>
                  <div className="w-7 h-7 rounded-full flex items-center justify-center text-[10px] font-bold font-mono shrink-0 mt-0.5"
                    style={{ background: `${ec}18`, border: `1px solid ${ec}55`, color: ec }}>
                    {msg.role === 'ivanna' ? 'IV' : 'TÚ'}
                  </div>
                  <div className={`max-w-[82%] rounded-2xl px-4 py-3 text-sm leading-relaxed ${
                    msg.role === 'ivanna'
                      ? 'bg-[#141C2A] border border-[#1E3050] text-[#CBD5E1] rounded-tl-sm'
                      : 'bg-[#12101E] border border-[#24184A] text-[#E2D9F3] rounded-tr-sm'
                  }`}>
                    <p>{msg.text}</p>
                    <div className="flex items-center justify-between mt-2 gap-3">
                      <span className="text-[10px] text-[#475569] font-mono">
                        {msg.ts.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })}
                        {msg.emotion && msg.emotion !== 'neutral' && (
                          <span className="ml-2" style={{ color: ec }}>• {msg.emotion}</span>
                        )}
                      </span>
                      <div className="flex gap-2">
                        {msg.role === 'ivanna' && (
                          <button onClick={() => speakText(msg.text)}
                            className="transition-colors" style={{ color: `${ec}66` }}
                            onMouseEnter={e => (e.currentTarget.style.color = ec)}
                            onMouseLeave={e => (e.currentTarget.style.color = `${ec}66`)}
                            title="Reproducir">
                            <Volume2 className="w-3 h-3" />
                          </button>
                        )}
                        {msg.audioBlob && (
                          <button onClick={() => {
                            const url = URL.createObjectURL(msg.audioBlob!);
                            Object.assign(document.createElement('a'), { href: url, download: `ivanna-rec-${msg.id}.webm` }).click();
                            setTimeout(() => URL.revokeObjectURL(url), 1000);
                          }} className="text-[10px] font-mono text-[#A855F7]/50 hover:text-[#A855F7] transition-colors" title="Descargar tu voz">
                            ⬇
                          </button>
                        )}
                      </div>
                    </div>
                  </div>
                </div>
              );
            })}

            {isThinking && (
              <div className="flex gap-3">
                <div className="w-7 h-7 rounded-full flex items-center justify-center text-[10px] font-bold font-mono bg-[#182230] border border-[#38BDF855] text-[#38BDF8] shrink-0">IV</div>
                <div className="bg-[#141C2A] border border-[#1E3050] rounded-2xl rounded-tl-sm px-4 py-3 flex items-center gap-2">
                  <Activity className="w-3.5 h-3.5 text-[#38BDF8] animate-pulse" />
                  <Loader2 className="w-3 h-3 text-[#38BDF8] animate-spin" />
                  <span className="text-xs text-[#64748B] font-mono">IVANNA procesa con Claude...</span>
                </div>
              </div>
            )}
            <div ref={messagesEndRef} />
          </div>

          {/* Input */}
          <div className="border-t border-[#1E2330] p-3 flex gap-2">
            <input type="text" value={textInput}
              onChange={e => setTextInput(e.target.value)}
              onKeyDown={e => e.key === 'Enter' && !e.shiftKey && handleSend()}
              placeholder="Escribe aquí tu mensaje o presiona el micrófono..."
              disabled={isListening || isThinking}
              className="flex-1 bg-[#12151C] border border-[#1E2330] rounded-lg px-3 py-2 text-sm text-[#E2E8F0] placeholder-[#334155] font-mono focus:outline-none focus:border-[#38BDF8] disabled:opacity-40 transition-colors" />
            <button onClick={handleSend} disabled={!textInput.trim() || isListening || isThinking}
              className="px-3 py-2 bg-[#182230] border border-[#38BDF8] text-[#38BDF8] rounded-lg hover:bg-[#1E2F44] transition-all disabled:opacity-30 disabled:cursor-not-allowed">
              <Send className="w-4 h-4" />
            </button>
          </div>
        </div>
      </div>
    </div>
  );
};
