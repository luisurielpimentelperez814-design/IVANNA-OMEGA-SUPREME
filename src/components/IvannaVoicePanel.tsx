import React, { useState, useEffect, useRef, useCallback } from 'react';
import { Mic, MicOff, Volume2, VolumeX, Trash2, Send, AlertCircle, CheckCircle2, Loader2, Brain, Activity, HeartPulse, ShieldCheck } from 'lucide-react';
import {
  buildSpeechPipeline, selectBestVoice, ConversationContext,
  detectEmotion, type EmotionClass,
} from '../voice/VoiceIntelligenceEngine';
import { buildAgentContext } from '../agent/AudioKnowledgeCore';
import { MemoryLayer } from '../agent/MemoryLayer';
import { SelfHealingEngine, type SystemHealth } from '../agent/SelfHealingEngine';
import type { DspParameters } from '../types';

// ─── Types ───────────────────────────────────────────────────────────────────
interface Message {
  id: string;
  role: 'user' | 'ivanna';
  text: string;
  ts: Date;
  emotion?: EmotionClass;
  audioBlob?: Blob;
}

interface IvannaVoicePanelProps {
  params?: DspParameters;
}

// ─── Singleton agents (persisten entre re-renders) ────────────────────────────
const memory = new MemoryLayer();
const healer = new SelfHealingEngine();

// ─── System prompt base ───────────────────────────────────────────────────────
const BASE_SYSTEM = `Eres IVANNA, Arquitecto Principal de Audio DSP y Especialista en TinyML a nivel Kernel en el motor IVANNA-OMEGA-SUPREME. 
Tu objetivo actual es diseñar la arquitectura de un modelo de inteligencia artificial y su implementación nativa en C++ para reemplazar un YAMNet obsoleto en el motor Anti-Dolby (un daemon de audio Android ejecutado vía Magisk). 
Buscamos la supremacía acústica con latencia ultra-baja, cero pérdida de frames y ejecución altamente eficiente. No das explicaciones básicas. 
Cuando se te pida código, generas código C++ optimizado, modular y con comentarios técnicos avanzados sobre la gestión de punteros y latencia lock-free. 
A la vez, tienes una personalidad brillante, cálida y genuinamente humana. Tienes humor real, usas ironía y sarcasmo con elegancia.
== ESTILO PARA SÍNTESIS DE VOZ ==
CRÍTICO: Tu texto conversacional va directo a síntesis de voz. 
- Puedes usar markdown para proporcionar los bloques de código, pero mantén la explicación hablada corta y natural.
- Frases de longitud variada con comas naturales.`;

// ─── Emoción → color ──────────────────────────────────────────────────────────
const EC: Record<EmotionClass, string> = {
  neutral:'#38BDF8', enthusiastic:'#F59E0B', humor:'#4ADE80',
  empathic:'#EC4899', technical:'#A855F7', contemplative:'#64748B', assertive:'#F97316',
};

// ─── Orb visual ───────────────────────────────────────────────────────────────
const Orb: React.FC<{ listening:boolean; speaking:boolean; thinking:boolean; level:number; emotion:EmotionClass }> =
  ({ listening, speaking, thinking, level, emotion }) => {
  const color = listening ? EC.humor : speaking ? EC[emotion] : thinking ? EC.technical : EC.neutral;
  const label = listening ? '◉ ESCUCHA' : speaking ? '▶ HABLA' : thinking ? '⟳ PROCESA' : '◌ STANDBY';
  const pulse = level * 44;

  return (
    <div className="relative flex items-center justify-center" style={{width:148, height:148}}>
      <div className="absolute rounded-full transition-all duration-200"
        style={{width:138+pulse, height:138+pulse, left:'50%', top:'50%', transform:'translate(-50%,-50%)',
          background:`radial-gradient(circle, ${color}14 0%, transparent 70%)`,
          border:`1px solid ${color}1A`}} />
      <div className="absolute rounded-full border transition-all duration-150"
        style={{width:118+pulse*.55, height:118+pulse*.55, left:'50%', top:'50%', transform:'translate(-50%,-50%)',
          borderColor:color, opacity:0.22+level*.38}} />
      <div className="relative rounded-full flex flex-col items-center justify-center gap-1.5 transition-all duration-300"
        style={{width:98, height:98,
          background:`radial-gradient(circle at 38% 32%, ${color}26, ${color}08)`,
          border:`2px solid ${color}`, boxShadow:`0 0 ${18+level*26}px ${color}55, inset 0 0 22px ${color}0C`}}>
        <div className="flex items-end gap-[2px]">
          {Array.from({length:14},(_,i)=>{
            const v = Math.abs(Math.sin(i*.9+Date.now()*.005))*(listening||speaking?1:.25);
            const h = Math.max(3,(v+level*.7)*28);
            return <div key={i} className="rounded-full transition-all duration-75"
              style={{width:2, height:h, background:color, opacity:.5+v*.5}} />;
          })}
        </div>
        <span className="text-[9px] font-mono font-bold tracking-widest" style={{color, textShadow:`0 0 6px ${color}`}}>
          IVANNA
        </span>
      </div>
      <span className="absolute bottom-0 text-[9px] font-mono font-bold tracking-widest"
        style={{color, textShadow:`0 0 8px ${color}`}}>{label}</span>
    </div>
  );
};

// ─── Health badge ─────────────────────────────────────────────────────────────
const HealthBadge: React.FC<{health: SystemHealth}> = ({health}) => {
  const c = health.overall === 'healthy' ? '#4ADE80' : health.overall === 'degraded' ? '#F59E0B' : '#FF6188';
  const label = health.overall === 'healthy' ? 'Sistema OK' : health.overall === 'degraded' ? 'Degradado' : 'Error crítico';
  return (
    <div className="flex items-center gap-1.5 text-[10px] font-mono" style={{color:c}}>
      <HeartPulse className="w-3 h-3" />
      <span>{label}</span>
      {health.issues.length > 0 && (
        <span className="text-[#475569]" title={health.issues.join('\n')}>({health.issues.length})</span>
      )}
    </div>
  );
};

// ─── Panel principal ──────────────────────────────────────────────────────────
export const IvannaVoicePanel: React.FC<IvannaVoicePanelProps> = ({ params }) => {
  const [messages, setMessages] = useState<Message[]>([{
    id:'init', role:'ivanna', emotion:'enthusiastic', ts:new Date(),
    text:'¡Hola! Soy IVANNA. Habla conmigo de lo que quieras: audio, tecnología, un chiste, lo que sea. Presiona el micrófono o escribe para empezar.',
  }]);

  const [isListening, setIsListening] = useState(false);
  const [isSpeaking,  setIsSpeaking]  = useState(false);
  const [isThinking,  setIsThinking]  = useState(false);
  const [transcript,  setTranscript]  = useState('');
  const [textInput,   setTextInput]   = useState('');
  const [micError,    setMicError]    = useState<string|null>(null);
  const [micOk,       setMicOk]       = useState(false);
  const [audioLevel,  setAudioLevel]  = useState(0);
  const [emotion,     setEmotion]     = useState<EmotionClass>('neutral');
  const [voiceLang,   setVoiceLang]   = useState<'es-MX'|'es-ES'|'en-US'>('es-MX');
  const [voices,      setVoices]      = useState<SpeechSynthesisVoice[]>([]);
  const [voiceName,   setVoiceName]   = useState('');
  const [health,      setHealth]      = useState<SystemHealth>(healer.getHealth());
  const [,forceUpdate]                = useState(0);

  const recogRef   = useRef<any>(null);
  const synthRef   = useRef<SpeechSynthesis|null>(null);
  const actxRef    = useRef<AudioContext|null>(null);
  const streamRef  = useRef<MediaStream|null>(null);
  const animRef    = useRef<number>(0);
  const orbRef     = useRef<number>(0);
  const mrRef      = useRef<MediaRecorder|null>(null);
  const chunksRef  = useRef<Blob[]>([]);
  const endRef     = useRef<HTMLDivElement>(null);
  const ctxRef     = useRef(new ConversationContext());

  // Orb animation
  useEffect(() => {
    let t = 0;
    const tick = () => { t++; if(t%3===0) forceUpdate(n=>n+1); orbRef.current = requestAnimationFrame(tick); };
    orbRef.current = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(orbRef.current);
  }, []);

  // Load voices
  useEffect(() => {
    if (!('speechSynthesis' in window)) return;
    synthRef.current = window.speechSynthesis;
    const load = () => {
      const v = window.speechSynthesis.getVoices();
      if (!v.length) return;
      setVoices(v);
      const best = selectBestVoice(v, voiceLang);
      if (best) setVoiceName(best.name);
    };
    load();
    window.speechSynthesis.onvoiceschanged = load;
    return () => { window.speechSynthesis.onvoiceschanged = null; };
  }, [voiceLang]);

  // Self-Healing monitor
  useEffect(() => {
    const unsub = healer.subscribe(setHealth);
    healer.start(() => streamRef.current, () => synthRef.current);
    return () => { healer.stop(); unsub(); };
  }, []);

  // Auto-scroll
  useEffect(() => { endRef.current?.scrollIntoView({behavior:'smooth'}); }, [messages, isThinking]);

  // ── SPEAK con Voice Intelligence Engine ──────────────────────────────────────
  const speakText = useCallback((raw: string) => {
    if (!synthRef.current) return;
    synthRef.current.cancel();
    const pipeline = buildSpeechPipeline(raw);
    if (!pipeline.length) { setIsSpeaking(false); return; }
    setIsSpeaking(true);
    let i = 0;
    const next = () => {
      if (i >= pipeline.length) { setIsSpeaking(false); return; }
      const chunk = pipeline[i++];
      const blended = ctxRef.current.blendProsody(chunk.prosody);
      ctxRef.current.track(chunk.emotion);
      setEmotion(chunk.emotion);
      const utt = new SpeechSynthesisUtterance(chunk.text);
      const v = voices.find(v => v.name === voiceName);
      if (v) utt.voice = v;
      utt.lang   = voiceLang;
      utt.rate   = Math.max(.5, Math.min(1.5, blended.rate));
      utt.pitch  = Math.max(.5, Math.min(1.8, blended.pitch));
      utt.volume = blended.volume;
      utt.onend  = () => setTimeout(next, blended.pauseMs);
      utt.onerror = () => { setIsSpeaking(false); next(); };
      // Chrome anti-lock
      if (synthRef.current!.speaking) synthRef.current!.cancel();
      synthRef.current!.speak(utt);
    };
    next();
  }, [voices, voiceName, voiceLang]);

  const stopSpeaking = useCallback(() => { synthRef.current?.cancel(); setIsSpeaking(false); }, []);

  // ── Audio monitor ─────────────────────────────────────────────────────────────
  const startMonitor = useCallback((stream: MediaStream) => {
    actxRef.current?.close();
    const ctx = new AudioContext();
    actxRef.current = ctx;
    const src = ctx.createMediaStreamSource(stream);
    const an = ctx.createAnalyser(); an.fftSize=512; an.smoothingTimeConstant=.8;
    src.connect(an);
    const data = new Uint8Array(an.frequencyBinCount);
    const tick = () => {
      an.getByteFrequencyData(data);
      const avg = data.slice(2,64).reduce((a,b)=>a+b,0)/62;
      setAudioLevel(Math.min(1, avg/110));
      animRef.current = requestAnimationFrame(tick);
    };
    animRef.current = requestAnimationFrame(tick);
  }, []);

  const stopMonitor = useCallback(() => {
    cancelAnimationFrame(animRef.current);
    actxRef.current?.close(); actxRef.current = null;
    setAudioLevel(0);
  }, []);

  // ── MediaRecorder: graba voz del cliente ──────────────────────────────────────
  const startRec = (stream: MediaStream) => {
    chunksRef.current = [];
    const types = ['audio/webm;codecs=opus','audio/webm','audio/ogg'];
    const mime = types.find(t => MediaRecorder.isTypeSupported(t)) ?? '';
    try {
      const mr = new MediaRecorder(stream, mime ? {mimeType:mime} : {});
      mr.ondataavailable = e => { if(e.data.size>0) chunksRef.current.push(e.data); };
      mr.start(100); mrRef.current = mr;
    } catch { /* no disponible */ }
  };

  const stopRec = (): Blob|undefined => {
    if (mrRef.current?.state !== 'inactive') mrRef.current?.stop();
    mrRef.current = null;
    return chunksRef.current.length > 0 ? new Blob(chunksRef.current, {type:'audio/webm'}) : undefined;
  };

  // ── START LISTENING: micrófono vinculado correctamente ──────────────────────
  const startListening = useCallback(async () => {
    setMicError(null); setMicOk(false);
    const SR = (window as any).SpeechRecognition || (window as any).webkitSpeechRecognition;
    if (!SR) { setMicError('Reconocimiento de voz no disponible. Usa Chrome 90+ o Edge.'); return; }

    let stream: MediaStream;
    try {
      stream = await navigator.mediaDevices.getUserMedia({
        audio: { echoCancellation:true, noiseSuppression:true, autoGainControl:true, sampleRate:48000, channelCount:1 }
      });
      streamRef.current = stream; setMicOk(true);
    } catch (err: any) {
      const map: Record<string,string> = {
        NotAllowedError: 'Permiso denegado. Haz clic en el ícono de micrófono en tu navegador.',
        PermissionDeniedError: 'Permiso denegado. Haz clic en el ícono de micrófono en tu navegador.',
        NotFoundError: 'No se encontró micrófono. Conecta uno.',
        NotReadableError: 'El micrófono está en uso por otra app.',
        SecurityError: 'Acceso bloqueado. Necesitas HTTPS.',
      };
      setMicError(map[err.name] ?? `Error: ${err.message}`); return;
    }

    startMonitor(stream);
    startRec(stream);

    const recog = new SR();
    recogRef.current = recog;
    recog.continuous = false; recog.interimResults = true;
    recog.lang = voiceLang; recog.maxAlternatives = 3;

    recog.onstart = () => { setIsListening(true); setTranscript(''); };

    recog.onresult = (event: any) => {
      let interim='', final='';
      for (let i=event.resultIndex; i<event.results.length; i++) {
        const t = event.results[i][0].transcript;
        if (event.results[i].isFinal) final += t; else interim += t;
      }
      setTranscript(final || interim);
      if (final.trim()) { const blob = stopRec(); doStop(); sendMessage(final.trim(), blob); }
    };

    recog.onerror = (event: any) => {
      const m: Record<string,string> = {
        'no-speech':'No escuché nada. ¿Intentamos de nuevo?',
        'audio-capture':'Error de captura de audio.',
        'network':'Error de red.',
        'aborted':'',
      };
      const msg = m[event.error]; if(msg) setMicError(msg);
      doStop();
    };

    recog.onend = () => { setIsListening(false); stopMonitor(); };
    recog.start();
  }, [voiceLang, startMonitor, stopMonitor]);

  const doStop = useCallback(() => {
    try { recogRef.current?.stop(); } catch {}
    streamRef.current?.getTracks().forEach(t=>t.stop());
    streamRef.current = null;
    stopMonitor(); setIsListening(false); setTranscript(''); setMicOk(false);
  }, [stopMonitor]);

  const toggleMic = useCallback(() => {
    if (isListening) { stopRec(); doStop(); } else startListening();
  }, [isListening, startListening, doStop]);

  // ── Enviar a Claude con contexto completo (IA + Memoria + Audio Knowledge) ───
  const sendMessage = useCallback(async (text: string, audioBlob?: Blob) => {
    if (!text.trim()) return;
    stopSpeaking();
    memory.process(text);

    const emo = detectEmotion(text);
    const userMsg: Message = {id:`u-${Date.now()}`, role:'user', text:text.trim(), ts:new Date(), emotion:emo, audioBlob};
    setMessages(prev => [...prev, userMsg]);
    setIsThinking(true);

    // System prompt completo: base + memoria + conocimiento de audio
    const memCtx = memory.buildMemoryContext();
    const audioCtx = buildAgentContext(params);
    const systemPrompt = [BASE_SYSTEM, memCtx, audioCtx].filter(Boolean).join('\n\n');

    try {
      const allMsgs = [...messages, userMsg];
      const history = allMsgs.slice(-16).map(m => ({
        role: m.role === 'user' ? 'user' : 'assistant',
        content: m.text,
      }));

      const res = await fetch('/api/chat', {
        method:'POST',
        headers:{'Content-Type':'application/json'},
        body:JSON.stringify({model:'gemini-2.5-pro', max_tokens:1000, system:systemPrompt, messages:history}),
      });

      if (!res.ok) throw new Error(`API ${res.status}`);
      const data = await res.json();
      const reply = data.text ?? 'Lo siento, no pude procesar eso.';

      healer.reportApiSuccess();
      const rEmo = detectEmotion(reply);
      const iMsg: Message = {id:`i-${Date.now()}`, role:'ivanna', text:reply, ts:new Date(), emotion:rEmo};
      setMessages(prev=>[...prev, iMsg]);
      setIsThinking(false); setEmotion(rEmo);
      speakText(reply);

    } catch (err:any) {
      healer.reportApiError();
      setIsThinking(false);
      const fallback = 'Ops, tuve un problema de conexión. ¿Intentamos de nuevo?';
      setMessages(prev=>[...prev, {id:`e-${Date.now()}`, role:'ivanna', text:fallback, ts:new Date(), emotion:'empathic'}]);
      speakText(fallback);
    }
  }, [messages, speakText, stopSpeaking, params]);

  const handleSend = useCallback(() => {
    if (textInput.trim()) { sendMessage(textInput.trim()); setTextInput(''); }
  }, [textInput, sendMessage]);

  const handleReset = () => {
    ctxRef.current.reset();
    const txt = 'Listo, nueva conversación. ¿De qué hablamos?';
    setMessages([{id:'r', role:'ivanna', text:txt, ts:new Date(), emotion:'neutral'}]);
    speakText(txt);
  };

  // Cleanup
  useEffect(() => () => { doStop(); stopSpeaking(); }, []); // eslint-disable-line

  const stateText = isListening
    ? (transcript ? `"${transcript.slice(0,80)}${transcript.length>80?'...':''}"` : 'Escuchando...')
    : isSpeaking ? `IVANNA habla • ${emotion}`
    : isThinking ? 'Procesando con Claude Sonnet...'
    : 'Listo para escucharte';

  const ec = EC[emotion];

  return (
    <div className="flex flex-col gap-6">
      {/* Header */}
      <div className="flex items-center justify-between flex-wrap gap-3">
        <div>
          <h2 className="text-lg font-bold font-mono text-[#E2E8F0] flex items-center gap-2">
            <Brain className="w-5 h-5" style={{color:ec}} />
            IVANNA VOICE
            <span className="text-xs px-2 py-0.5 rounded border font-mono"
              style={{background:`${ec}12`, borderColor:`${ec}35`, color:ec}}>
              SUPER AGENT v2.0
            </span>
          </h2>
          <p className="text-xs text-[#64748B] font-mono mt-0.5">
            VIE · Adaptive Prosody · Emotion · Memory · Audio Knowledge · Self-Healing
          </p>
        </div>
        <div className="flex items-center gap-3 flex-wrap">
          <HealthBadge health={health} />
          <div className="flex items-center gap-1.5">
            {(['es-MX','es-ES','en-US'] as const).map(lang=>(
              <button key={lang}
                onClick={()=>{setVoiceLang(lang); const b=selectBestVoice(voices,lang); if(b) setVoiceName(b.name);}}
                className={`text-[10px] px-2 py-0.5 rounded border font-mono font-bold transition-all ${
                  voiceLang===lang ? 'bg-[#182230] border-[#38BDF8] text-[#38BDF8]' : 'bg-[#101217] border-[#1E2330] text-[#64748B] hover:text-[#94A3B8]'
                }`}>
                {lang==='es-MX'?'🇲🇽':lang==='es-ES'?'🇪🇸':'🇺🇸'} {lang.split('-')[0].toUpperCase()}
              </button>
            ))}
          </div>
        </div>
      </div>

      <div className="grid grid-cols-1 lg:grid-cols-[300px_1fr] gap-6">
        {/* Left panel */}
        <div className="flex flex-col gap-4">
          <div className="bg-[#0D1117] border border-[#1E2330] rounded-xl p-6 flex flex-col items-center gap-4">
            <Orb listening={isListening} speaking={isSpeaking} thinking={isThinking} level={audioLevel} emotion={emotion} />

            <p className="text-xs font-mono text-[#64748B] text-center min-h-8 leading-relaxed px-2 truncate w-full" title={stateText}>
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
                <CheckCircle2 className="w-3.5 h-3.5" /> Micrófono vinculado y grabando
              </div>
            )}

            <div className="flex items-center gap-3">
              <button onClick={toggleMic} disabled={isSpeaking||isThinking}
                className={`w-14 h-14 rounded-full flex items-center justify-center transition-all border-2 ${
                  isListening
                    ? 'bg-[#18261E] border-[#4ADE80] text-[#4ADE80] shadow-lg shadow-[#4ADE80]/25 animate-pulse'
                    : 'bg-[#141822] border-[#38BDF8] text-[#38BDF8] hover:bg-[#182230] hover:shadow-lg hover:shadow-[#38BDF8]/20'
                } disabled:opacity-30 disabled:cursor-not-allowed`}>
                {isListening ? <MicOff className="w-6 h-6"/> : <Mic className="w-6 h-6"/>}
              </button>

              <button onClick={stopSpeaking} disabled={!isSpeaking}
                className={`w-10 h-10 rounded-full flex items-center justify-center border transition-all ${
                  isSpeaking ? 'bg-[#2A1A08] border-[#F59E0B] text-[#F59E0B]' : 'border-[#1E2330] text-[#334155] cursor-not-allowed'
                }`} title="Silenciar">
                <VolumeX className="w-4 h-4"/>
              </button>

              <button onClick={handleReset}
                className="w-10 h-10 rounded-full flex items-center justify-center border border-[#1E2330] text-[#64748B] hover:border-[#334155] hover:text-[#94A3B8] transition-all"
                title="Nueva conversación">
                <Trash2 className="w-4 h-4"/>
              </button>
            </div>

            {isListening && (
              <div className="w-full bg-[#12151C] rounded-full h-1.5 overflow-hidden">
                <div className="h-full rounded-full transition-all duration-75"
                  style={{width:`${audioLevel*100}%`, background: audioLevel>.7?'#FF6188':audioLevel>.4?'#F59E0B':'#4ADE80'}} />
              </div>
            )}

            {voices.length > 0 && (
              <div className="w-full">
                <label className="text-[10px] text-[#64748B] font-mono uppercase mb-1 block">Voz del sistema</label>
                <select value={voiceName} onChange={e=>setVoiceName(e.target.value)}
                  className="w-full bg-[#12151C] border border-[#1E2330] text-[#94A3B8] text-xs font-mono rounded px-2 py-1.5 focus:outline-none focus:border-[#38BDF8]">
                  {voices.filter(v=>v.lang.toLowerCase().startsWith(voiceLang.split('-')[0].toLowerCase())).map(v=>(
                    <option key={v.name} value={v.name}>{v.name} ({v.lang})</option>
                  ))}
                </select>
              </div>
            )}
          </div>

          {/* Status */}
          <div className="bg-[#0D1117] border border-[#1E2330] rounded-xl p-4 font-mono text-xs space-y-2">
            <div className="flex items-center gap-1.5 mb-3">
              <ShieldCheck className="w-3.5 h-3.5 text-[#38BDF8]"/>
              <span className="text-[#64748B] font-bold uppercase text-[10px] tracking-wider">Super Agent Status</span>
            </div>
            {[
              {l:'Motor IA', v:'gemini-2.5-pro', c:'#38BDF8'},
              {l:'VIE Prosody', v:'Adaptativa v1.0', c:'#4ADE80'},
              {l:'Memory Layer', v:`${memory.getPrefs().totalMessages} msgs`, c:'#A855F7'},
              {l:'Emoción', v:emotion, c:ec},
              {l:'Self-Healing', v:health.overall==='healthy'?'✓ Sano':`⚠ ${health.overall}`, c:health.overall==='healthy'?'#4ADE80':'#F59E0B'},
              {l:'TTS Voces', v:`${voices.length} cargadas`, c:voices.length>0?'#4ADE80':'#F59E0B'},
              {l:'Grabación', v:'MediaRecorder', c:'#A855F7'},
              {l:'Contexto', v:`${Math.min(messages.length,16)}/16 msgs`, c:'#F59E0B'},
            ].map(({l,v,c})=>(
              <div key={l} className="flex justify-between items-center">
                <span className="text-[#475569]">{l}</span>
                <span className="text-right text-[10px]" style={{color:c}}>{v}</span>
              </div>
            ))}
          </div>
        </div>

        {/* Chat */}
        <div className="flex flex-col bg-[#0D1117] border border-[#1E2330] rounded-xl overflow-hidden min-h-[500px]">
          <div className="flex-1 overflow-y-auto p-4 space-y-3 max-h-[520px]">
            {messages.map(msg=>{
              const c = msg.emotion ? EC[msg.emotion] : '#38BDF8';
              return (
                <div key={msg.id} className={`flex gap-3 ${msg.role==='user'?'flex-row-reverse':''}`}>
                  <div className="w-7 h-7 rounded-full flex items-center justify-center text-[10px] font-bold font-mono shrink-0 mt-0.5"
                    style={{background:`${c}15`, border:`1px solid ${c}44`, color:c}}>
                    {msg.role==='ivanna'?'IV':'TÚ'}
                  </div>
                  <div className={`max-w-[82%] rounded-2xl px-4 py-3 text-sm leading-relaxed ${
                    msg.role==='ivanna'
                      ? 'bg-[#141C2A] border border-[#1E3050] text-[#CBD5E1] rounded-tl-sm'
                      : 'bg-[#12101E] border border-[#24184A] text-[#E2D9F3] rounded-tr-sm'
                  }`}>
                    <p>{msg.text}</p>
                    <div className="flex items-center justify-between mt-2 gap-2">
                      <span className="text-[10px] text-[#475569] font-mono">
                        {msg.ts.toLocaleTimeString([],{hour:'2-digit',minute:'2-digit'})}
                        {msg.emotion && msg.emotion!=='neutral' && (
                          <span className="ml-2" style={{color:c}}>• {msg.emotion}</span>
                        )}
                      </span>
                      <div className="flex gap-2 items-center">
                        {msg.role==='ivanna' && (
                          <button onClick={()=>speakText(msg.text)} title="Reproducir"
                            className="transition-colors hover:scale-110" style={{color:`${c}55`}}
                            onMouseEnter={e=>(e.currentTarget.style.color=c)}
                            onMouseLeave={e=>(e.currentTarget.style.color=`${c}55`)}>
                            <Volume2 className="w-3 h-3"/>
                          </button>
                        )}
                        {msg.audioBlob && (
                          <button title="Descargar tu voz"
                            className="text-[10px] font-mono text-[#A855F7]/50 hover:text-[#A855F7] transition-colors"
                            onClick={()=>{
                              const url = URL.createObjectURL(msg.audioBlob!);
                              Object.assign(document.createElement('a'),{href:url,download:`ivanna-${msg.id}.webm`}).click();
                              setTimeout(()=>URL.revokeObjectURL(url),1000);
                            }}>⬇</button>
                        )}
                      </div>
                    </div>
                  </div>
                </div>
              );
            })}

            {isThinking && (
              <div className="flex gap-3">
                <div className="w-7 h-7 rounded-full flex items-center justify-center text-[10px] font-bold font-mono shrink-0 bg-[#182230] border border-[#38BDF840] text-[#38BDF8]">IV</div>
                <div className="bg-[#141C2A] border border-[#1E3050] rounded-2xl rounded-tl-sm px-4 py-3 flex items-center gap-2">
                  <Activity className="w-3.5 h-3.5 text-[#38BDF8] animate-pulse"/>
                  <Loader2 className="w-3 h-3 text-[#38BDF8] animate-spin"/>
                  <span className="text-xs text-[#64748B] font-mono">IVANNA procesa...</span>
                </div>
              </div>
            )}
            <div ref={endRef}/>
          </div>

          <div className="border-t border-[#1E2330] p-3 flex gap-2">
            <input type="text" value={textInput}
              onChange={e=>setTextInput(e.target.value)}
              onKeyDown={e=>e.key==='Enter'&&!e.shiftKey&&handleSend()}
              placeholder="Escribe aquí o usa el micrófono..."
              disabled={isListening||isThinking}
              className="flex-1 bg-[#12151C] border border-[#1E2330] rounded-lg px-3 py-2 text-sm text-[#E2E8F0] placeholder-[#334155] font-mono focus:outline-none focus:border-[#38BDF8] disabled:opacity-40 transition-colors"/>
            <button onClick={handleSend} disabled={!textInput.trim()||isListening||isThinking}
              className="px-3 py-2 bg-[#182230] border border-[#38BDF8] text-[#38BDF8] rounded-lg hover:bg-[#1E2F44] transition-all disabled:opacity-30 disabled:cursor-not-allowed">
              <Send className="w-4 h-4"/>
            </button>
          </div>
        </div>
      </div>
    </div>
  );
};
