#!/usr/bin/env node
// ivanna_hal_mmap_bounded.js - Sonda solo lectura para enlace HAL real
// No usa offsets, no hace dlsym especulativo, solo verifica correlación frames+timestamp
// Moto G85 blair - AudioOut_D 13 y AudioOut_15 21

const fs = require('fs');
const { execSync } = require('child_process');

function sh(cmd) {
  try { return execSync(cmd, {encoding:'utf8'}); } catch(e) { return "ERR: "+e.message; }
}

console.log("=== IVANNA HAL MMAP SHELL PROBE v2.5.205 ===");
console.log(new Date().toISOString());

// 1. Verificar que libar-pal y audio.primary.blair existen
console.log("\n[1] BINARIOS HAL");
console.log(sh("ls -lh /vendor/lib64/libar-pal.so /vendor/lib64/hw/audio.primary.blair.so 2>&1 | head -5"));
console.log(sh("su -c 'strings /vendor/lib64/hw/audio.primary.blair.so' | grep -E 'GetMmapPosition|pal_stream_get_mmap_position|GetFramesWritten' | head -20"));

// 2. Ver símbolos exportados (no asumidos)
console.log("\n[2] SIMBOLOS EXPORTADOS -D");
console.log(sh("su -c 'nm -D /vendor/lib64/hw/audio.primary.blair.so' | grep -i mmap | head -20"));

// 3. Runtime actual de streams
console.log("\n[3] RUNTIME AUDIO ACTUAL");
let runtime = "";
try {
  runtime = sh("su -c 'cat /data/local/tmp/ivanna_audio_runtime.txt' 2>&1 | tail -100");
  console.log(runtime);
} catch {}
console.log(sh("su -c 'cat /data/local/tmp/ivanna_master_audit.txt' 2>&1 | grep -E 'AudioOut_D|AudioOut_15|handle|localSR|rate' | tail -30"));

// 4. Logcat de mmap
console.log("\n[4] LOGCAT MMAP");
console.log(sh("su -c 'logcat -d | grep -i \"mmap\\|pal_stream\\|GetMmapPosition\\|framesWritten\"' | tail -50"));

// 5. Prueba de correlación: leer 3 veces con intervalo
console.log("\n[5] PRUEBA CORRELACION 3 MUESTRAS (pon musica en altavoz)");
for(let i=0;i<3;i++) {
  let t = Date.now();
  let log = sh("su -c 'logcat -d | grep -E \"AudioOut_D.*rate|AudioOut_15.*rate\" | tail -5'");
  console.log(`--- Muestra ${i+1} ts=${t} ---`);
  console.log(log);
  execSync("sleep 2");
}

console.log("\n=== FIN SONDA ===");
console.log("NOTA: esta sonda de shell NO llama ni engancha pal_stream_get_mmap_position: solo lista simbolos/logs. No mide frames+timestamp.");
console.log("Para la medicion real usa ivanna_hal_mmap_bounded.js (Frida, engancha pasivo).");
console.log("CRITERIO (PAL devuelve 0 = exito): rc==0 con frames que avanzan y tiempo monotonico -> candidato a enlace; rc!=0 o sin llamadas -> el stream no es MMAP, no cablear.");
