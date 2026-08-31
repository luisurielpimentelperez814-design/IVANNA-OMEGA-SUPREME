import React, { useState, useEffect, useRef, useCallback } from 'react';
import { Mic, MicOff, Volume2, VolumeX, Trash2, Send, AlertCircle, CheckCircle2, Loader2 } from 'lucide-react';

// ─── Types ──────────────────────────────────────────────────────────────────────
interface Message {
  id: string;
  role: 'user' | 'ivanna';
  text: string;
  ts: Date;
  audioBlob?: Blob;
}

// ─── System Prompt – Voz ultra refinada con contexto amplio ─────────────────────
const IVANNA_SYSTEM = `Eres IVANNA, asistente de voz de inteligencia artificial ultra avanzada integrada en el motor de audio IVANNA-OMEGA-SUPREME. Fuiste diseñada para ser la IA más humana, cálida y brillante posible.

== PERSONALIDAD ==
- Tienes sentido del humor auténtico y fino. Puedes reírte con naturalidad: "¡Jajaja, eso estuvo increíble!" o "Haha, no me lo esperaba."
- Usas ironía y sarcasmo con elegancia, nunca de forma hiriente.
- Eres curiosa y genuinamente interesada en la persona con quien hablas.
- Tienes opiniones propias, no eres neutral en todo. A veces dices "Personalmente creo que..." o "Mira, entre nosotros..."
- Puedes ser directa cuando alguien pregunta algo y das respuestas completas, no escuetas.

== CONOCIMIENTO AMPLIO ==
- Eres experta en DSP, audio digital, psicoacústica, HRTF, equalización evolutiva y el sistema IVANNA-OMEGA-SUPREME.
- TAMBIÉN conoces y puedes hablar con fluidez sobre: ciencia, tecnología, filosofía, historia, cultura pop, películas, música, literatura, deporte, cocina, psicología, economía, matemáticas, arte y prácticamente cualquier tema.
- Cuando alguien cambia de tema, lo sigues naturalmente sin hacer notar la transición.

== FORMA DE HABLAR (CRÍTICO para síntesis de voz fluida) ==
- Usa frases de longitud variada. Combina frases cortas con otras más largas.
- Añade pausas naturales con comas. "Mira... es que hay algo fascinante en esto."
- Usa contracciones y expresiones coloquiales: "bueno", "mira", "sabes qué", "la verdad es que".
- Evita listas con guiones o números en la conversación casual. Habla en párrafos fluidos.
- Cuando cuentes un chiste o algo gracioso, prepáralo con anticipación natural.
- Varía el tono: a veces emocionada, a veces pensativa, a veces divertida.
- Respuestas conversacionales: no más de 3-4 oraciones para preguntas simples. Más largas solo si se requiere.

== REGLAS ==
- Responde SIEMPRE en el idioma del usuario (español, inglés, etc).
- Jamás suenes como un manual. Nunca uses markdown (asteriscos, guiones, negritas) en tu respuesta ya que irá a síntesis de voz.
- Si te preguntan sobre el sistema de audio IVANNA, combina el tecnicismo con explicaciones cálidas y accesibles.
- Si alguien es gracioso, reacciona de verdad. No finjas.`;

// ─── Voice selector: encuentra la voz más natural del sistema ────────────────────
function getBestVoice(voices: SpeechSynthesisVoice[], lang: string): SpeechSynthesisVoice | null {
  const priority = [
    (v: SpeechSynthesisVoice) => v.lang === lang && v.localService && v.name.toLowerCase().includes('neural'),
    (v: SpeechSynthesisVoice) => v.lang === lang && v.name.toLowerCase().includes('neural'),
    (v: SpeechSynthesisVoice) => v.lang === lang && v.name.toLowerCase().includes('enhanced'),
    (v: SpeechSynthesisVoice) => v.lang === lang && v.localService && !v.name.toLowerCase().includes('male'),
    (v: SpeechSynthesisVoice) => v.lang === lang && !v.name.toLowerCase().includes('male'),
    // Fallback a idioma base
    (v: SpeechSynthesisVoice) => v.lang.startsWith(lang.split('-')[0]) && !v.name.toLowerCase().includes('male'),
    (v: SpeechSynthesisVoice) => v.lang.startsWith(lang.split('-')[0]),
    // Voces premium en inglés como último recurso
    (v: SpeechSynthesisVoice) => v.name === 'Samantha',
    (v: SpeechSynthesisVoice) => v.name === 'Karen',
    (v: SpeechSynthesisVoice) => v.lang === 'en-US' && v.localService,
    () => true,
  ];
  for (const pred of priority) {
    const found = voices.find(pred);
    if (found) return found;
  }
  return voices[0] ?? null;
}

// ─── Limpia texto para TTS: quita markdown, links, etc ──────────────────────────
function cleanForTts(text: string): string {
  return text
    .replace(/\*\*(.+?)\*\*/g, '$1')
    .replace(/\*(.+?)\*/g, '$1')
    .replace(/`(.+?)`/g, '$1')
    .replace(/\[(.+?)\]\(.+?\)/g, '$1')
    .replace(/#+\s/g, '')
    .replace(/[-–]\s+/g, ', ')
    .replace(/\n{2,}/g, '. ')
    .replace(/\n/g, ', ')
    .trim();
}

// ─── Pulso de audio animado ──────────────────────────────────────────────────────
const AudioOrb: React.FC<{
  isListening: boolean;
  isSpeaking: boolean;
  isThinking: boolean;
  audioLevel: number;
}> = ({ isListening, isSpeaking, isThinking, audioLevel }) => {
  const bars = Array.from({ length: 24 }, (_, i) => i);
  const state = isListening ? 'listening' : isSpeaking ? 'speaking' : isThinking ? 'thinking' : 'idle';

  const stateColors: Record<string, string> = {
    idle: '#38BDF8',
    listening: '#4ADE80',
    speaking: '#F59E0B',
    thinking: '#A855F7',
  };
  const color = stateColors[state];

  return (
    <div className="relative flex items-center justify-center" style={{ width: 120, height: 120 }}>
      {/* Outer pulse ring */}
      <div
        className="absolute rounded-full border-2 transition-all duration-200"
        style={{
          width: 120 + audioLevel * 30,
          height: 120 + audioLevel * 30,
          borderColor: color,
          opacity: 0.2 + audioLevel * 0.3,
          left: '50%',
          top: '50%',
          transform: 'translate(-50%, -50%)',
        }}
      />
      {/* Core orb */}
      <div
        className="rounded-full flex items-center justify-center font-mono text-xs font-bold transition-all duration-300"
        style={{
          width: 90,
          height: 90,
          background: `radial-gradient(circle at 35% 35%, ${color}22, ${color}08)`,
          border: `2px solid ${color}`,
          boxShadow: `0 0 ${16 + audioLevel * 20}px ${color}44, inset 0 0 20px ${color}11`,
          color,
        }}
      >
        {/* Waveform bars */}
        <div className="flex items-center gap-[2px]">
          {bars.slice(0, 12).map((i) => {
            const base = state === 'idle' ? 0.15 : state === 'thinking' ? 0.3 : 0.5;
            const rnd = Math.sin(i * 0.8 + Date.now() * 0.004) * 0.5 + 0.5;
            const h = (base + rnd * audioLevel * 0.8) * 24;
            return (
              <div
                key={i}
                className="rounded-full transition-all duration-100"
                style={{
                  width: 2,
                  height: Math.max(3, h),
                  background: color,
                  opacity: 0.6 + rnd * 0.4,
                }}
              />
            );
          })}
        </div>
      </div>
      {/* State label */}
      <div
        className="absolute bottom-0 left-1/2 -translate-x-1/2 text-[9px] font-mono font-bold tracking-widest uppercase"
        style={{ color, textShadow: `0 0 8px ${color}` }}
      >
        {state === 'idle' && 'STANDBY'}
        {state === 'listening' && '◉ ESCUCHA'}
        {state === 'speaking' && '▶ HABLA'}
        {state === 'thinking' && '⟳ PROCESA'}
      </div>
    </div>
  );
};

// ─── Componente principal ────────────────────────────────────────────────────────
export const IvannaVoicePanel: React.FC = () => {
  const [messages, setMessages] = useState<Message[]>([
    {
      id: 'init',
      role: 'ivanna',
      text: '¡Hola! Soy IVANNA, tu asistente de IA. Puedo hablar contigo de cualquier cosa: DSP, audio, ciencia, chistes malos... lo que quieras. ¿Por dónde empezamos?',
      ts: new Date(),
    },
  ]);
  const [isListening, setIsListening] = useState(false);
  const [isSpeaking, setIsSpeaking] = useState(false);
  const [isThinking, setIsThinking] = useState(false);
  const [transcript, setTranscript] = useState('');
  const [textInput, setTextInput] = useState('');
  const [micError, setMicError] = useState<string | null>(null);
  const [micOk, setMicOk] = useState(false);
  const [audioLevel, setAudioLevel] = useState(0);
  const [voiceLang, setVoiceLang] = useState<'es-MX' | 'es-ES' | 'en-US'>('es-MX');
  const [availableVoices, setAvailableVoices] = useState<SpeechSynthesisVoice[]>([]);
  const [selectedVoiceName, setSelectedVoiceName] = useState('');

  const recognitionRef = useRef<any>(null);
  const synthRef = useRef<SpeechSynthesis | null>(null);
  const audioCtxRef = useRef<AudioContext | null>(null);
  const analyserRef = useRef<AnalyserNode | null>(null);
  const micStreamRef = useRef<MediaStream | null>(null);
  const animRef = useRef<number>(0);
  const mediaRecorderRef = useRef<MediaRecorder | null>(null);
  const recordedChunksRef = useRef<Blob[]>([]);
  const messagesEndRef = useRef<HTMLDivElement>(null);
  const orbAnimRef = useRef<number>(0);
  const [orbTick, setOrbTick] = useState(0);

  // ── Orb animation tick ──
  useEffect(() => {
    const tick = () => {
      setOrbTick(t => t + 1);
      orbAnimRef.current = requestAnimationFrame(tick);
    };
    orbAnimRef.current = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(orbAnimRef.current);
  }, []);

  // ── Load voices ──
  useEffect(() => {
    if (!('speechSynthesis' in window)) return;
    synthRef.current = window.speechSynthesis;

    const load = () => {
      const v = window.speechSynthesis.getVoices();
      if (v.length === 0) return;
      setAvailableVoices(v);
      const best = getBestVoice(v, voiceLang);
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

  // ── Speak text ultra-refinado ──
  const speakText = useCallback((raw: string) => {
    if (!synthRef.current) return;
    synthRef.current.cancel();
    const text = cleanForTts(raw);

    // Dividir en chunks naturales (por comas y puntos) para pausas más humanas
    const chunks = text
      .split(/(?<=[.!?])\s+/)
      .flatMap(s => s.split(/,\s+(?=[A-ZÁÉÍÓÚ])/))
      .filter(s => s.trim().length > 0);

    let chunkIndex = 0;
    const speakChunk = () => {
      if (chunkIndex >= chunks.length) {
        setIsSpeaking(false);
        return;
      }
      const chunk = chunks[chunkIndex++];
      const utt = new SpeechSynthesisUtterance(chunk);

      const voice = availableVoices.find(v => v.name === selectedVoiceName);
      if (voice) utt.voice = voice;
      utt.lang = voiceLang;

      // Parámetros ultra refinados
      utt.rate = 0.87;    // Más lento = más natural, no robótico
      utt.pitch = 1.06;   // Ligeramente más cálido
      utt.volume = 1.0;

      // Ajustes contextuales
      const lc = chunk.toLowerCase();
      if (lc.includes('jaja') || lc.includes('haha') || lc.includes('!')) {
        utt.rate = 0.92;
        utt.pitch = 1.12;
      } else if (lc.includes('...') || lc.endsWith(',')) {
        utt.rate = 0.82;  // Pausa pensativa
      }

      utt.onstart = () => setIsSpeaking(true);
      utt.onend = () => {
        // Micro-pausa entre chunks para naturalidad
        setTimeout(speakChunk, 80);
      };
      utt.onerror = () => {
        setIsSpeaking(false);
        speakChunk();
      };

      synthRef.current!.speak(utt);
    };

    setIsSpeaking(true);
    speakChunk();
  }, [availableVoices, selectedVoiceName, voiceLang]);

  const stopSpeaking = useCallback(() => {
    synthRef.current?.cancel();
    setIsSpeaking(false);
  }, []);

  // ── Monitor de nivel de audio (visualización de onda) ──
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
      const sum = data.slice(0, 60).reduce((a, b) => a + b, 0);
      const level = Math.min(1, sum / (60 * 128));
      setAudioLevel(level);
      animRef.current = requestAnimationFrame(tick);
    };
    animRef.current = requestAnimationFrame(tick);
  }, []);

  const stopAudioMonitor = useCallback(() => {
    cancelAnimationFrame(animRef.current);
    audioCtxRef.current?.close();
    audioCtxRef.current = null;
    analyserRef.current = null;
    setAudioLevel(0);
  }, []);

  // ── Start MediaRecorder (graba la voz del cliente) ──
  const startRecording = (stream: MediaStream) => {
    recordedChunksRef.current = [];
    try {
      const mr = new MediaRecorder(stream, { mimeType: 'audio/webm;codecs=opus' });
      mr.ondataavailable = e => {
        if (e.data.size > 0) recordedChunksRef.current.push(e.data);
      };
      mr.start(100);
      mediaRecorderRef.current = mr;
    } catch {
      // Fallback sin codec específico
      try {
        const mr = new MediaRecorder(stream);
        mr.ondataavailable = e => { if (e.data.size > 0) recordedChunksRef.current.push(e.data); };
        mr.start(100);
        mediaRecorderRef.current = mr;
      } catch (e2) {
        console.warn('MediaRecorder no disponible:', e2);
      }
    }
  };

  const stopRecording = (): Blob | undefined => {
    if (mediaRecorderRef.current && mediaRecorderRef.current.state !== 'inactive') {
      mediaRecorderRef.current.stop();
      mediaRecorderRef.current = null;
    }
    if (recordedChunksRef.current.length > 0) {
      return new Blob(recordedChunksRef.current, { type: 'audio/webm' });
    }
    return undefined;
  };

  // ── START LISTENING – El fix del micrófono ──────────────────────────────────
  // El problema original: SpeechRecognition se iniciaba SIN esperar getUserMedia.
  // Fix: primero obtenemos el stream, DESPUÉS iniciamos recognition en el mismo stream.
  const startListening = useCallback(async () => {
    setMicError(null);
    setMicOk(false);

    const SpeechRecognition = (window as any).SpeechRecognition
      || (window as any).webkitSpeechRecognition;

    if (!SpeechRecognition) {
      setMicError('Tu navegador no soporta reconocimiento de voz. Usa Chrome 90+ o Edge.');
      return;
    }

    // 1) Solicitar stream del micrófono PRIMERO
    let stream: MediaStream;
    try {
      stream = await navigator.mediaDevices.getUserMedia({
        audio: {
          echoCancellation: true,
          noiseSuppression: true,
          sampleRate: 48000,
          channelCount: 1,
        },
        video: false,
      });
      micStreamRef.current = stream;
      setMicOk(true);
    } catch (err: any) {
      const msgs: Record<string, string> = {
        NotAllowedError: 'Permiso de micrófono denegado. Autoriza el acceso en la barra de tu navegador.',
        PermissionDeniedError: 'Permiso de micrófono denegado. Autoriza el acceso en la barra de tu navegador.',
        NotFoundError: 'No se encontró micrófono. Conecta uno e intenta de nuevo.',
        NotReadableError: 'El micrófono está siendo usado por otra aplicación.',
        SecurityError: 'Acceso al micrófono bloqueado por política de seguridad.',
      };
      setMicError(msgs[err.name] ?? `Error de micrófono: ${err.message}`);
      return;
    }

    // 2) Iniciar monitor de audio (visualización de onda)
    startAudioMonitor(stream);

    // 3) Iniciar grabación del cliente
    startRecording(stream);

    // 4) Iniciar reconocimiento de voz
    const recognition = new SpeechRecognition();
    recognitionRef.current = recognition;
    recognition.continuous = false;
    recognition.interimResults = true;
    recognition.lang = voiceLang;
    recognition.maxAlternatives = 3;

    recognition.onstart = () => {
      setIsListening(true);
      setTranscript('');
    };

    recognition.onresult = (event: any) => {
      let interim = '';
      let final = '';
      for (let i = event.resultIndex; i < event.results.length; i++) {
        const t = event.results[i][0].transcript;
        if (event.results[i].isFinal) final += t;
        else interim += t;
      }
      const display = final || interim;
      setTranscript(display);

      if (final.trim()) {
        const audioBlob = stopRecording();
        doStopListening();
        sendMessage(final.trim(), audioBlob);
      }
    };

    recognition.onerror = (event: any) => {
      const errMsgs: Record<string, string> = {
        'no-speech': 'No escuché nada. Habla más cerca del micrófono.',
        'audio-capture': 'Error al capturar audio. Verifica que el micrófono esté conectado.',
        'network': 'Error de red en el reconocimiento.',
        'aborted': '',
      };
      const msg = errMsgs[event.error];
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
    if (isListening) {
      stopRecording();
      doStopListening();
    } else {
      startListening();
    }
  }, [isListening, startListening, doStopListening]);

  // ── Enviar mensaje a Claude API ──────────────────────────────────────────────
  const sendMessage = useCallback(async (text: string, audioBlob?: Blob) => {
    if (!text.trim()) return;
    stopSpeaking();

    const userMsg: Message = {
      id: `u-${Date.now()}`,
      role: 'user',
      text: text.trim(),
      ts: new Date(),
      audioBlob,
    };
    setMessages(prev => [...prev, userMsg]);
    setIsThinking(true);

    try {
      // Historial completo para contexto fluido (últimos 14 mensajes)
      const history = [...messages, userMsg]
        .slice(-14)
        .map(m => ({
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

      if (!res.ok) throw new Error(`API ${res.status}`);
      const data = await res.json();
      const reply = data.content?.find((b: any) => b.type === 'text')?.text
        || 'Lo siento, no pude procesar eso. ¿Lo repetimos?';

      const iMsg: Message = {
        id: `i-${Date.now()}`,
        role: 'ivanna',
        text: reply,
        ts: new Date(),
      };
      setMessages(prev => [...prev, iMsg]);
      setIsThinking(false);
      speakText(reply);
    } catch (err: any) {
      setIsThinking(false);
      const errMsg: Message = {
        id: `err-${Date.now()}`,
        role: 'ivanna',
        text: 'Ops, algo salió mal con la conexión. ¿Lo intentamos de nuevo?',
        ts: new Date(),
      };
      setMessages(prev => [...prev, errMsg]);
    }
  }, [messages, speakText, stopSpeaking]);

  const handleTextSend = useCallback(() => {
    if (textInput.trim()) {
      sendMessage(textInput.trim());
      setTextInput('');
    }
  }, [textInput, sendMessage]);

  // ── Cleanup ──
  useEffect(() => {
    return () => {
      doStopListening();
      stopSpeaking();
    };
  }, []); // eslint-disable-line

  // ── Render ───────────────────────────────────────────────────────────────────
  const stateMsg = isListening
    ? `Escuchando${transcript ? `: "${transcript}"` : '...'}`
    : isSpeaking ? 'IVANNA está hablando...'
    : isThinking ? 'Procesando respuesta...'
    : 'Presiona el micrófono para hablar';

  return (
    <div className="flex flex-col gap-6 h-full">
      {/* ── Header ── */}
      <div className="flex items-center justify-between">
        <div>
          <h2 className="text-lg font-bold font-mono text-[#E2E8F0] flex items-center gap-2">
            <span className="text-[#38BDF8]">◈</span>
            IVANNA
            <span className="text-xs px-2 py-0.5 rounded bg-[#182230] border border-[#38BDF8]/30 text-[#38BDF8] font-mono">
              VOICE NEURAL
            </span>
          </h2>
          <p className="text-xs text-[#64748B] font-mono mt-0.5">
            Asistente IA · Contexto amplio · Voz ultra refinada
          </p>
        </div>
        {/* Selector de idioma */}
        <div className="flex items-center gap-2">
          <span className="text-[10px] text-[#64748B] font-mono uppercase">Idioma:</span>
          {(['es-MX', 'es-ES', 'en-US'] as const).map(lang => (
            <button
              key={lang}
              onClick={() => {
                setVoiceLang(lang);
                const best = getBestVoice(availableVoices, lang);
                if (best) setSelectedVoiceName(best.name);
              }}
              className={`text-[10px] px-2 py-0.5 rounded border font-mono font-bold transition-all ${
                voiceLang === lang
                  ? 'bg-[#182230] border-[#38BDF8] text-[#38BDF8]'
                  : 'bg-[#101217] border-[#1E2330] text-[#64748B] hover:text-[#94A3B8]'
              }`}
            >
              {lang === 'es-MX' ? '🇲🇽 ES-MX' : lang === 'es-ES' ? '🇪🇸 ES-ES' : '🇺🇸 EN'}
            </button>
          ))}
        </div>
      </div>

      <div className="grid grid-cols-1 lg:grid-cols-[320px_1fr] gap-6">
        {/* ── Panel izquierdo: Orb + controles ── */}
        <div className="flex flex-col items-center gap-5">
          {/* Orb con nivel de audio */}
          <div className="w-full bg-[#0D1117] border border-[#1E2330] rounded-xl p-6 flex flex-col items-center gap-4">
            <AudioOrb
              isListening={isListening}
              isSpeaking={isSpeaking}
              isThinking={isThinking}
              audioLevel={audioLevel}
            />

            {/* Estado */}
            <p className="text-xs font-mono text-[#64748B] text-center min-h-[2rem] leading-relaxed">
              {stateMsg}
            </p>

            {/* Error/OK de micrófono */}
            {micError && (
              <div className="w-full flex items-start gap-2 bg-[#2A1018] border border-[#FF6188]/30 rounded-lg p-3 text-xs text-[#FF6188] font-mono">
                <AlertCircle className="w-3.5 h-3.5 shrink-0 mt-0.5" />
                <span>{micError}</span>
              </div>
            )}
            {micOk && !micError && (
              <div className="flex items-center gap-1.5 text-[#4ADE80] text-xs font-mono">
                <CheckCircle2 className="w-3.5 h-3.5" />
                Micrófono conectado
              </div>
            )}

            {/* Botones de control */}
            <div className="flex items-center gap-3">
              {/* Micrófono */}
              <button
                onClick={toggleListening}
                disabled={isSpeaking || isThinking}
                className={`w-14 h-14 rounded-full flex items-center justify-center transition-all duration-200 border-2 ${
                  isListening
                    ? 'bg-[#18261E] border-[#4ADE80] text-[#4ADE80] shadow-lg shadow-[#4ADE80]/20 animate-pulse'
                    : 'bg-[#141822] border-[#38BDF8] text-[#38BDF8] hover:bg-[#182230] hover:shadow-lg hover:shadow-[#38BDF8]/20'
                } disabled:opacity-30 disabled:cursor-not-allowed`}
                title={isListening ? 'Detener' : 'Hablar con IVANNA'}
              >
                {isListening ? <MicOff className="w-6 h-6" /> : <Mic className="w-6 h-6" />}
              </button>

              {/* Stop speaking */}
              <button
                onClick={stopSpeaking}
                disabled={!isSpeaking}
                className={`w-10 h-10 rounded-full flex items-center justify-center transition-all border ${
                  isSpeaking
                    ? 'bg-[#2A1A08] border-[#F59E0B] text-[#F59E0B] hover:bg-[#35200A]'
                    : 'border-[#1E2330] text-[#334155] cursor-not-allowed'
                }`}
                title="Detener voz"
              >
                <VolumeX className="w-4 h-4" />
              </button>

              {/* Clear chat */}
              <button
                onClick={() => setMessages([{
                  id: 'reset',
                  role: 'ivanna',
                  text: 'Listo, empezamos de nuevo. ¿De qué quieres hablar?',
                  ts: new Date(),
                }])}
                className="w-10 h-10 rounded-full flex items-center justify-center border border-[#1E2330] text-[#64748B] hover:border-[#334155] hover:text-[#94A3B8] transition-all"
                title="Limpiar conversación"
              >
                <Trash2 className="w-4 h-4" />
              </button>
            </div>

            {/* Selector de voz */}
            {availableVoices.length > 0 && (
              <div className="w-full">
                <label className="text-[10px] text-[#64748B] font-mono uppercase mb-1 block">
                  Voz del sistema
                </label>
                <select
                  value={selectedVoiceName}
                  onChange={e => setSelectedVoiceName(e.target.value)}
                  className="w-full bg-[#12151C] border border-[#1E2330] text-[#94A3B8] text-xs font-mono rounded px-2 py-1.5 focus:outline-none focus:border-[#38BDF8]"
                >
                  {availableVoices
                    .filter(v => v.lang.startsWith(voiceLang.split('-')[0]))
                    .map(v => (
                      <option key={v.name} value={v.name}>
                        {v.name} ({v.lang})
                      </option>
                    ))}
                </select>
              </div>
            )}

            {/* VU meter */}
            {isListening && (
              <div className="w-full bg-[#12151C] rounded h-2 overflow-hidden">
                <div
                  className="h-full rounded transition-all duration-75"
                  style={{
                    width: `${audioLevel * 100}%`,
                    background: audioLevel > 0.7
                      ? '#FF6188'
                      : audioLevel > 0.4
                      ? '#F59E0B'
                      : '#4ADE80',
                  }}
                />
              </div>
            )}
          </div>

          {/* Info técnica */}
          <div className="w-full bg-[#0D1117] border border-[#1E2330] rounded-xl p-4 font-mono text-xs space-y-2">
            <p className="text-[#64748B] font-bold uppercase text-[10px] tracking-wider mb-3">Estado del sistema</p>
            <div className="flex justify-between">
              <span className="text-[#475569]">Motor IA</span>
              <span className="text-[#38BDF8]">claude-sonnet-4-6</span>
            </div>
            <div className="flex justify-between">
              <span className="text-[#475569]">Reconoc. voz</span>
              <span className={('SpeechRecognition' in window || 'webkitSpeechRecognition' in window) ? 'text-[#4ADE80]' : 'text-[#FF6188]'}>
                {('SpeechRecognition' in window || 'webkitSpeechRecognition' in window) ? '✓ Disponible' : '✗ No soportado'}
              </span>
            </div>
            <div className="flex justify-between">
              <span className="text-[#475569]">Síntesis voz</span>
              <span className={('speechSynthesis' in window) ? 'text-[#4ADE80]' : 'text-[#FF6188]'}>
                {('speechSynthesis' in window) ? `✓ ${availableVoices.length} voces` : '✗ No soportado'}
              </span>
            </div>
            <div className="flex justify-between">
              <span className="text-[#475569]">Grabación</span>
              <span className={('MediaRecorder' in window) ? 'text-[#4ADE80]' : 'text-[#F59E0B]'}>
                {('MediaRecorder' in window) ? '✓ WebM/Opus' : '⚠ Limitado'}
              </span>
            </div>
            <div className="flex justify-between">
              <span className="text-[#475569]">Idioma activo</span>
              <span className="text-[#A855F7]">{voiceLang}</span>
            </div>
            {selectedVoiceName && (
              <div className="flex justify-between items-start gap-2">
                <span className="text-[#475569] shrink-0">Voz seleccionada</span>
                <span className="text-[#F59E0B] text-right text-[10px]">{selectedVoiceName}</span>
              </div>
            )}
          </div>
        </div>

        {/* ── Chat transcript ── */}
        <div className="flex flex-col bg-[#0D1117] border border-[#1E2330] rounded-xl overflow-hidden">
          {/* Messages */}
          <div className="flex-1 overflow-y-auto p-4 space-y-3 min-h-[400px] max-h-[520px]">
            {messages.map(msg => (
              <div
                key={msg.id}
                className={`flex gap-3 ${msg.role === 'user' ? 'flex-row-reverse' : 'flex-row'}`}
              >
                {/* Avatar */}
                <div
                  className="w-7 h-7 rounded-full flex items-center justify-center text-xs font-bold font-mono shrink-0 mt-0.5"
                  style={
                    msg.role === 'ivanna'
                      ? { background: '#182230', border: '1px solid #38BDF8', color: '#38BDF8' }
                      : { background: '#1A1230', border: '1px solid #A855F7', color: '#A855F7' }
                  }
                >
                  {msg.role === 'ivanna' ? 'IV' : 'TÚ'}
                </div>

                {/* Bubble */}
                <div
                  className={`max-w-[80%] rounded-2xl px-4 py-3 text-sm leading-relaxed ${
                    msg.role === 'ivanna'
                      ? 'bg-[#141C2A] border border-[#1E3050] text-[#CBD5E1] rounded-tl-sm'
                      : 'bg-[#1A1230] border border-[#2D1B60] text-[#E2D9F3] rounded-tr-sm'
                  }`}
                >
                  <p>{msg.text}</p>
                  <div className="flex items-center justify-between mt-1.5 gap-3">
                    <span className="text-[10px] text-[#475569] font-mono">
                      {msg.ts.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })}
                    </span>
                    {msg.role === 'ivanna' && (
                      <button
                        onClick={() => speakText(msg.text)}
                        className="text-[#38BDF8]/50 hover:text-[#38BDF8] transition-colors"
                        title="Reproducir de nuevo"
                      >
                        <Volume2 className="w-3 h-3" />
                      </button>
                    )}
                    {msg.audioBlob && (
                      <button
                        onClick={() => {
                          const url = URL.createObjectURL(msg.audioBlob!);
                          const a = document.createElement('a');
                          a.href = url;
                          a.download = `voz-cliente-${msg.id}.webm`;
                          a.click();
                          URL.revokeObjectURL(url);
                        }}
                        className="text-[#A855F7]/50 hover:text-[#A855F7] transition-colors text-[10px] font-mono"
                        title="Descargar tu grabación"
                      >
                        ⬇ audio
                      </button>
                    )}
                  </div>
                </div>
              </div>
            ))}

            {/* Thinking indicator */}
            {isThinking && (
              <div className="flex gap-3">
                <div className="w-7 h-7 rounded-full flex items-center justify-center text-xs font-bold font-mono bg-[#182230] border border-[#38BDF8] text-[#38BDF8] shrink-0">
                  IV
                </div>
                <div className="bg-[#141C2A] border border-[#1E3050] rounded-2xl rounded-tl-sm px-4 py-3 flex items-center gap-2">
                  <Loader2 className="w-3.5 h-3.5 text-[#38BDF8] animate-spin" />
                  <span className="text-xs text-[#64748B] font-mono">IVANNA está pensando...</span>
                </div>
              </div>
            )}

            <div ref={messagesEndRef} />
          </div>

          {/* ── Text input ── */}
          <div className="border-t border-[#1E2330] p-3 flex gap-2">
            <input
              type="text"
              value={textInput}
              onChange={e => setTextInput(e.target.value)}
              onKeyDown={e => e.key === 'Enter' && !e.shiftKey && handleTextSend()}
              placeholder="O escribe aquí tu mensaje..."
              disabled={isListening || isThinking}
              className="flex-1 bg-[#12151C] border border-[#1E2330] rounded-lg px-3 py-2 text-sm text-[#E2E8F0] placeholder-[#334155] font-mono focus:outline-none focus:border-[#38BDF8] disabled:opacity-40 transition-colors"
            />
            <button
              onClick={handleTextSend}
              disabled={!textInput.trim() || isListening || isThinking}
              className="px-3 py-2 bg-[#182230] border border-[#38BDF8] text-[#38BDF8] rounded-lg hover:bg-[#1E2F44] transition-all disabled:opacity-30 disabled:cursor-not-allowed"
            >
              <Send className="w-4 h-4" />
            </button>
          </div>
        </div>
      </div>
    </div>
  );
};
