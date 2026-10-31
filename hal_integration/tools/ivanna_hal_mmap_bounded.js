'use strict';

// Bounded, passive capture of the HAL's StreamOutPrimary::GetMmapPosition().
// v2.5.206: hooks a real 244-byte HAL function instead of the 4-byte PAL thunk
// (a trampoline over a tiny thunk can corrupt neighbouring code and crash the
// audio HAL). It refuses to hook any target smaller than MIN_BYTES, never calls
// PAL itself and detaches after LIMIT returns.
const MODULE = 'audio.primary.blair.so';
const SYMBOL = '_ZN16StreamOutPrimary15GetMmapPositionEP19audio_mmap_position';
const MIN_BYTES = 32;
const LIMIT = 300;

const mod = Process.findModuleByName(MODULE);
if (!mod) {
  console.log('[ERROR] module not loaded: ' + MODULE);
} else {
  const sym = mod.enumerateSymbols().find(s => s.name === SYMBOL);
  if (!sym) {
    console.log('[ERROR] symbol not found: ' + SYMBOL);
  } else if (!sym.size || sym.size < MIN_BYTES) {
    console.log('[REFUSED] ' + SYMBOL + ' is ' + sym.size + ' bytes (< ' + MIN_BYTES + '); not hooking');
  } else {
    console.log('[HOOK] ' + SYMBOL + ' @ ' + sym.address + ' size=' + sym.size);
    const stats = Object.create(null);
    let total = 0;
    const listener = Interceptor.attach(sym.address, {
      onEnter(args) { this.self = args[0].toString(); this.out = args[1]; },
      onLeave(retval) {
        total++;
        const st = stats[this.self] || (stats[this.self] = { calls: 0, ok: 0, last: null });
        st.calls++;
        const rc = retval.toInt32();
        let raw = 'n/a', frames = 'n/a', ns = 'n/a', dF = 'n/a', dNs = 'n/a';
        if (rc === 0) {
          st.ok++;
          try {
            raw = hexdump(this.out, { length: 16, header: false, ansi: false }).split('\n')[0];
            // Android audio_mmap_position: int64 time_nanoseconds; int32 position_frames.
            const t = BigInt(this.out.readS64().toString());
            const f = this.out.add(8).readS32();
            ns = t.toString(); frames = String(f);
            if (st.last) { dF = String((f - st.last.f) | 0); dNs = (t - st.last.t).toString(); }
            st.last = { f, t };
          } catch (e) {}
        }
        if (st.calls <= 2 || st.calls % 50 === 0) {
          console.log('[SAMPLE] total=' + total + ' stream=' + this.self + ' n=' + st.calls +
            ' rc=' + rc + ' raw=' + raw + ' frames=' + frames + ' ns=' + ns +
            ' dFrames=' + dF + ' dNs=' + dNs);
        }
        if (total >= LIMIT) {
          listener.detach();
          console.log('[DONE] stopped after ' + total + ' calls');
          console.log('[STATS] ' + JSON.stringify(stats, (k, v) => (k === 'last' ? undefined : v)));
        }
      }
    });
    console.log('[READY] bounded to ' + LIMIT + ' returns; no [SAMPLE] means this stream never calls it (not MMAP).');
  }
}
