'use strict';

// Bounded, passive capture of pal_stream_get_mmap_position() as called by the
// installed HAL. It never calls PAL itself, writes nothing and detaches after LIMIT.
// Run with Frida against the audio HAL process while music plays; record the route.
const LIMIT = 600;
const names = ['pal_stream_get_mmap_position'];
let addr = null;
for (const m of ['audio.primary.blair.so', 'libar-pal.so', 'libpalclient.so']) {
  const mod = Process.findModuleByName(m);
  if (!mod) continue;
  const hit = mod.enumerateExports().concat(mod.enumerateImports())
    .find(s => names.indexOf(s.name) >= 0 && s.address && !s.address.isNull());
  if (hit) { addr = hit.address; console.log('[HOOK] ' + hit.name + ' via ' + m + ' @ ' + addr); break; }
}
if (!addr) {
  console.log('[ERROR] pal_stream_get_mmap_position not resolvable in the loaded modules');
} else {
  const stats = Object.create(null);
  let total = 0;
  const listener = Interceptor.attach(addr, {
    onEnter(args) { this.handle = args[0].toString(); this.out = args[1]; },
    onLeave(retval) {
      total++;
      const st = stats[this.handle] || (stats[this.handle] = { calls: 0, ok: 0, last: null });
      st.calls++;
      const rc = retval.toInt32();
      let raw = 'n/a', frames = 'n/a', ns = 'n/a', dF = 'n/a', dNs = 'n/a';
      if (rc === 0) {
        st.ok++;
        try {
          raw = hexdump(this.out, { length: 16, header: false, ansi: false }).split('\n')[0];
          // Provisional decode under the PUBLIC layout (may differ on this build).
          const t = BigInt(this.out.readS64().toString());
          const f = this.out.add(8).readU32();
          ns = t.toString(); frames = String(f);
          if (st.last) {
            dF = String((f - st.last.f) >>> 0);
            dNs = (t - st.last.t).toString();
          }
          st.last = { f, t };
        } catch (e) {}
      }
      if (st.calls <= 2 || st.calls % 50 === 0) {
        console.log('[SAMPLE] total=' + total + ' handle=' + this.handle + ' n=' + st.calls +
          ' rc=' + rc + ' raw=' + raw + ' frames?=' + frames + ' ns?=' + ns +
          ' dFrames?=' + dF + ' dNs?=' + dNs);
      }
      if (total >= LIMIT) {
        listener.detach();
        console.log('[DONE] stopped after ' + total + ' calls');
        console.log('[STATS] ' + JSON.stringify(stats, (k, v) => (k === 'last' ? undefined : v)));
      }
    }
  });
  console.log('[READY] bounded to ' + LIMIT + ' returns; play audio, then wait for [DONE].');
}
