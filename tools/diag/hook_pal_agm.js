'use strict';
// hook_pal_agm.js — SOLO OBSERVA. No modifica argumentos ni valores de retorno.
// Compatible con Frida 16 y 17. Los layouts de structs NO se asumen: se vuelcan bytes crudos.
// Uso: frida -U -p <PID> -l hook_pal_agm.js -o frida_<PID>.log   (adjuntar ANTES de reproducir)

const T0 = Date.now();
const CAP = {};            // tope de líneas por función (evita inundar con timestamps)
const LIMITS = { pal_get_timestamp: 300, pal_stream_get_mmap_position: 300 };
const DEFAULT_LIMIT = 800;

function emit(o) { o.t = ((Date.now() - T0) / 1000).toFixed(3); o.pid = Process.id; console.log(JSON.stringify(o)); }
function allowed(fn) { CAP[fn] = (CAP[fn] || 0) + 1; return CAP[fn] <= (LIMITS[fn] || DEFAULT_LIMIT); }

function hex(p, n) {
  try { if (p === null || p.isNull()) return null;
        return Array.from(new Uint8Array(p.readByteArray(n))).map(b => ('0' + b.toString(16)).slice(-2)).join(''); }
  catch (e) { return 'unreadable'; }
}
function cstr(p) { try { return p.isNull() ? null : p.readUtf8String(); } catch (e) { return null; } }
function bt(ctx) {
  try { return Thread.backtrace(ctx, Backtracer.ACCURATE).slice(0, 6).map(DebugSymbol.fromAddress).map(String); }
  catch (e) { return []; }
}

// resolución de exports compatible 16/17, con fallback por enumeración
function findExport(modName, sym) {
  let m = null;
  try { m = Process.findModuleByName(modName); } catch (e) {}
  if (!m) return null;
  try { if (typeof m.findExportByName === 'function') { const a = m.findExportByName(sym); if (a) return a; } } catch (e) {}
  try { if (typeof Module.findExportByName === 'function') { const a = Module.findExportByName(modName, sym); if (a) return a; } } catch (e) {}
  try { const hit = m.enumerateExports().find(x => x.name === sym); if (hit) return hit.address; } catch (e) {}
  return null;
}
function findAnywhere(sym, mods) {
  for (const mn of mods) { const a = findExport(mn, sym); if (a) return { addr: a, mod: mn }; }
  return null;
}

const PAL_MODS  = ['libpalclient.so', 'libar-pal.so'];
const AGM_MODS  = ['libagm.so', 'libagmclient.so'];
const TINY_MODS = ['libtinyalsa.so', 'libar-pal.so', 'libagm.so'];

let mixerCtlName = null;
(function () {
  const r = findAnywhere('mixer_ctl_get_name', TINY_MODS);
  if (r) mixerCtlName = new NativeFunction(r.addr, 'pointer', ['pointer']);
})();
function ctlName(ctl) { try { return mixerCtlName ? cstr(mixerCtlName(ctl)) : null; } catch (e) { return null; } }

let hooked = [];
function hook(sym, mods, onEnter, onLeave, withBt) {
  const r = findAnywhere(sym, mods);
  if (!r) { emit({ ev: 'absent', fn: sym }); return; }
  Interceptor.attach(r.addr, {
    onEnter(args) { this.a = [args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]];
                    this.keep = allowed(sym);
                    if (this.keep && onEnter) { try { onEnter.call(this, this.a); } catch (e) { emit({ ev: 'hook_err', fn: sym, err: String(e) }); } }
                    if (this.keep && withBt) this.bt = bt(this.context); },
    onLeave(rv) { if (!this.keep) return;
                  try { if (onLeave) onLeave.call(this, rv, this.a); } catch (e) { emit({ ev: 'hook_err', fn: sym, err: String(e) }); }
                  emit({ ev: 'ret', fn: sym, rv: rv.toInt32(), bt: this.bt || undefined }); }
  });
  hooked.push(sym + '@' + r.mod);
}

// ---------- PAL (libpalclient / libar-pal) ----------
hook('pal_stream_open', PAL_MODS, function (a) {
  emit({ ev: 'call', fn: 'pal_stream_open', n_dev: a[1].toUInt32(), n_mod: a[3].toUInt32(),
         attr_hex: hex(a[0], 128), dev0_hex: hex(a[2], 160), handle_out: a[7].toString() });
  this.hout = a[7];
}, function () { try { emit({ ev: 'pal_stream_open.handle', handle: this.hout.readPointer().toString() }); } catch (e) {} }, true);

hook('pal_stream_set_param', PAL_MODS, function (a) {
  emit({ ev: 'call', fn: 'pal_stream_set_param', handle: a[0].toString(), param_id: '0x' + a[1].toUInt32().toString(16), payload_hex: hex(a[2], 96) });
}, null, true);

hook('pal_stream_get_param', PAL_MODS, function (a) { this.pp = a[2];
  emit({ ev: 'call', fn: 'pal_stream_get_param', handle: a[0].toString(), param_id: '0x' + a[1].toUInt32().toString(16) });
}, function () { try { emit({ ev: 'pal_stream_get_param.out', payload_hex: hex(this.pp.readPointer(), 96) }); } catch (e) {} }, false);

hook('pal_set_param', PAL_MODS, function (a) {
  const n = a[2].toUInt32();
  emit({ ev: 'call', fn: 'pal_set_param', param_id: '0x' + a[0].toUInt32().toString(16), size: n, payload_hex: hex(a[1], Math.min(n, 128)) });
}, null, true);

hook('pal_get_param', PAL_MODS, function (a) { this.pp = a[1]; this.ps = a[2];
  emit({ ev: 'call', fn: 'pal_get_param', param_id: '0x' + a[0].toUInt32().toString(16) });
}, function () { try { emit({ ev: 'pal_get_param.out', size: this.ps.readU64().toString(), payload_hex: hex(this.pp.readPointer(), 96) }); } catch (e) {} }, false);

hook('pal_stream_set_device', PAL_MODS, function (a) {
  emit({ ev: 'call', fn: 'pal_stream_set_device', handle: a[0].toString(), n_dev: a[1].toUInt32(), dev_hex: hex(a[2], 160) });
}, null, true);

hook('pal_stream_set_buffer_size', PAL_MODS, function (a) {
  emit({ ev: 'call', fn: 'pal_stream_set_buffer_size', handle: a[0].toString(), in_hex: hex(a[1], 16), out_hex: hex(a[2], 16) });
}, null, false);

hook('pal_get_timestamp', PAL_MODS, function (a) { this.o = a[1]; this.h = a[0]; }, function () {
  emit({ ev: 'ts', fn: 'pal_get_timestamp', handle: this.h.toString(), out_hex: hex(this.o, 32) });
}, false);

hook('pal_stream_get_mmap_position', PAL_MODS, function (a) { this.o = a[1]; this.h = a[0]; }, function () {
  emit({ ev: 'ts', fn: 'pal_stream_get_mmap_position', handle: this.h.toString(), out_hex: hex(this.o, 32) });
}, false);

// ---------- AGM ----------
hook('agm_session_set_params', AGM_MODS, function (a) {
  const n = a[2].toUInt32();
  emit({ ev: 'call', fn: 'agm_session_set_params', session: a[0].toString(), size: n, payload_hex: hex(a[1], Math.min(n, 192)) });
}, null, true);

hook('agm_session_set_non_tunnel_mode_config', AGM_MODS, function (a) {
  emit({ ev: 'call', fn: 'agm_session_set_non_tunnel_mode_config', a0: a[0].toString(), a1: a[1].toString(), a2: a[2].toString(), cfg_hex: hex(a[1], 128) });
}, null, true);

// ---------- tinyalsa: ¿se escribe algún control de razón/reloj al reproducir USB? ----------
hook('mixer_ctl_set_enum_by_string', TINY_MODS, function (a) {
  emit({ ev: 'mixer', fn: 'set_enum_by_string', ctl: ctlName(a[0]), value: cstr(a[1]) });
}, null, true);
hook('mixer_ctl_set_value', TINY_MODS, function (a) {
  emit({ ev: 'mixer', fn: 'set_value', ctl: ctlName(a[0]), idx: a[1].toInt32(), value: a[2].toInt32() });
}, null, true);
hook('mixer_ctl_set_array', TINY_MODS, function (a) {
  emit({ ev: 'mixer', fn: 'set_array', ctl: ctlName(a[0]), count: a[2].toUInt32(), array_hex: hex(a[1], 64) });
}, null, true);

emit({ ev: 'ready', hooked: hooked, modules: Process.enumerateModules().map(m => m.name).filter(n => /pal|agm|tinyalsa|audio/i.test(n)) });
