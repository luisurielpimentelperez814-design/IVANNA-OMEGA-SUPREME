'use strict';

// Bounded capture of the legacy HAL presentation-position callback.
// This records each stream pointer independently; it does not infer ASRC state.
const symbolName = '_ZL37astream_out_get_presentation_positionPK16audio_stream_outPmP8timespec';
const moduleName = 'audio.primary.blair.so';
const LIMIT = 1200;
const mod = Process.findModuleByName(moduleName);
if (!mod) {
  console.log('[ERROR] HAL module not loaded: ' + moduleName);
} else {
  const sym = mod.enumerateSymbols().find(s => s.name === symbolName);
  if (!sym) {
    console.log('[ERROR] presentation-position symbol not found');
  } else {
    const counts = Object.create(null);
    const previous = Object.create(null);
    let total = 0;
    let listener = null;
    listener = Interceptor.attach(sym.address, {
      onEnter(args) {
        this.stream = args[0].toString();
        this.framesPtr = args[1];
        this.timePtr = args[2];
        this.wallMs = Date.now();
      },
      onLeave(retval) {
        total++;
        const stream = this.stream;
        counts[stream] = (counts[stream] || 0) + 1;
        let frames = 'error', sec = 'error', nsec = 'error';
        try {
          frames = this.framesPtr.readU64().toString();
          sec = this.timePtr.readS64().toString();
          nsec = this.timePtr.add(8).readS64().toString();
        } catch (e) {}
        let deltaFrames = 'n/a', deltaMs = 'n/a';
        const prev = previous[stream];
        if (prev && /^\d+$/.test(frames) && /^\d+$/.test(prev.frames)) {
          try {
            deltaFrames = (BigInt(frames) - BigInt(prev.frames)).toString();
            deltaMs = (this.wallMs - prev.wallMs).toFixed(3);
          } catch (e) {}
        }
        previous[stream] = { frames, wallMs: this.wallMs };
        if (counts[stream] <= 2 || counts[stream] % 100 === 0) {
          console.log('[SAMPLE] total=' + total +
            ' stream=' + stream + ' n=' + counts[stream] +
            ' retval=' + retval + ' frames=' + frames +
            ' delta_frames=' + deltaFrames + ' delta_ms=' + deltaMs +
            ' ts=' + sec + '.' + nsec);
        }
        if (total >= LIMIT) {
          listener.detach();
          console.log('[DONE] Bounded capture stopped after ' + total + ' calls.');
          console.log('[COUNTS] ' + JSON.stringify(counts));
        }
      }
    });
    console.log('[READY] Bounded to ' + LIMIT + ' callback returns; play audio, then wait for [DONE].');
  }
}
