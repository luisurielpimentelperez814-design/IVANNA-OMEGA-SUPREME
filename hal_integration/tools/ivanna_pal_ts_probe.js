'use strict';
// ivanna_pal_ts_probe.js — SOLO LECTURA. Mide si pal_get_timestamp() entrega, en el
// proceso del HAL de audio, algun campo correlacionado con CLOCK_MONOTONIC.
// Hace: engancha pal_get_timestamp, guarda los 3 campos de 64 bit de pal_session_time
// (session_time, absolute_time, timestamp, en us) y CLOCK_MONOTONIC antes/despues de
// cada llamada. No escribe memoria del HAL, no cambia argumentos ni retorno.
// Requiere audio sonando por la salida USB mientras corre (~25 s).
const LIMIT = 500, MAX_MS = 25000;
const libc = Process.findModuleByName('libc.so');
const cgAddr = libc.findExportByName ? libc.findExportByName('clock_gettime')
                                      : Module.findExportByName('libc.so', 'clock_gettime');
const cg = new NativeFunction(cgAddr, 'int', ['int', 'pointer']);
const tb = Memory.alloc(16);
function monoUs() {
  cg(1, tb);
  return Number(tb.readS64()) * 1e6 + Number(tb.add(8).readS64()) / 1e3;
}
function findExp(mod, sym) {
  const m = Process.findModuleByName(mod);
  if (!m) return null;
  return m.findExportByName ? m.findExportByName(sym) : Module.findExportByName(mod, sym);
}
const S = {}; // por modulo
let total = 0;
const hooks = [];
['libar-pal.so', 'libpalclient.so'].forEach(function (mn) {
  const a = findExp(mn, 'pal_get_timestamp');
  console.log('[probe] ' + mn + ' pal_get_timestamp = ' + (a ? a : 'no cargado/sin export'));
  if (!a) return;
  S[mn] = [];
  hooks.push(Interceptor.attach(a, {
    onEnter(args) { this.t0 = monoUs(); this.p = args[1]; },
    onLeave(ret) {
      const t1 = monoUs();
      if (ret.toInt32() !== 0 || total >= LIMIT) return;
      try {
        const f = [0, 8, 16].map(function (o) { return this.p.add(o).readU64().toNumber(); }, this);
        S[mn].push({ mid: (this.t0 + t1) / 2, dur: t1 - this.t0, f: f });
        total++;
      } catch (e) {}
    }
  }));
});
if (!hooks.length) console.log('[ERROR] ninguna libreria PAL con pal_get_timestamp en este proceso: pid equivocado?');

function fit(xs, ys) { // regresion lineal y = a + b x
  const n = xs.length; let mx = 0, my = 0;
  for (let i = 0; i < n; i++) { mx += xs[i]; my += ys[i]; }
  mx /= n; my /= n; let sxx = 0, sxy = 0;
  for (let i = 0; i < n; i++) { sxx += (xs[i] - mx) * (xs[i] - mx); sxy += (xs[i] - mx) * (ys[i] - my); }
  const b = sxy / sxx; let ss = 0;
  for (let i = 0; i < n; i++) { const r = ys[i] - (my + b * (xs[i] - mx)); ss += r * r; }
  return { slopePpm: (b - 1) * 1e6, residStdUs: Math.sqrt(ss / n) };
}
function report() {
  hooks.forEach(function (h) { h.detach(); });
  Object.keys(S).forEach(function (mn) {
    const d = S[mn];
    console.log('=== ' + mn + ': muestras=' + d.length);
    if (d.length < 30) { console.log('  pocas muestras (¿hay audio sonando por USB?)'); return; }
    const durs = d.map(function (s) { return s.dur; }).sort(function (a, b) { return a - b; });
    console.log('  duracion de la llamada us: mediana=' + durs[durs.length >> 1].toFixed(0) + ' p95=' + durs[Math.floor(durs.length * 0.95)].toFixed(0) + ' max=' + durs[durs.length - 1].toFixed(0));
    ['session_time', 'absolute_time', 'timestamp'].forEach(function (nm, k) {
      const xs = d.map(function (s) { return s.mid; }), ys = d.map(function (s) { return s.f[k]; });
      const distinct = new Set(ys).size;
      const r = fit(xs, ys);
      console.log('  ' + nm + ': distintos=' + distinct + ' primero=' + ys[0] + ' ultimo=' + ys[ys.length - 1] +
        ' pendiente_vs_MONOTONIC=' + (r.slopePpm >= 0 ? '+' : '') + r.slopePpm.toFixed(1) + ' ppm residuo_std=' + r.residStdUs.toFixed(1) + ' us');
    });
  });
  console.log('[probe] fin. Lectura: un campo con pendiente cercana a 0 ppm y residuo pequeño (decenas de us) esta en un reloj coherente con CLOCK_MONOTONIC; residuo ~290 us o multiplos de 1000 indica cuantizacion a ms.');
}
setTimeout(report, MAX_MS);
console.log('[probe] capturando ' + MAX_MS / 1000 + ' s (max ' + LIMIT + ' muestras)...');
