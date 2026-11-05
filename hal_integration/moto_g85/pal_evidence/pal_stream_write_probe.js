// Frida (SOLO LECTURA): registra llamadas a pal_stream_write en el HAL de audio. No altera buffers.
//   frida -U -n android.hardware.audio.service.qti -l pal_stream_write_probe.js   (ajusta el nombre del proceso)
// Datos que entrega: handle, puntero y tamano del buffer, primeras palabras de pal_buffer y tasa de llamadas.
// Con esto se cierran los pendientes del informe: ABI de pal_buffer, formato/tasa reales, si el PCM de la app pasa por aqui.
const m = Process.findModuleByName('audio.primary.blair.so') || Process.findModuleByName('libar-pal.so');
const f = Module.findExportByName(null, 'pal_stream_write') ||
          (m && m.findExportByName('pal_stream_write'));
if (!f) { console.log('pal_stream_write no encontrado'); } else {
  let n = 0, t0 = Date.now();
  Interceptor.attach(f, { onEnter(a) {
    n++;
    if (n <= 20 || n % 500 === 0) {
      const h = a[0], pb = a[1];
      const words = [0,1,2,3,4,5].map(i => { try { return pb.add(i*8).readU64().toString(16); } catch (e) { return '??'; } });
      const hz = (n * 1000 / Math.max(1, Date.now() - t0)).toFixed(1);
      console.log(JSON.stringify({ n, handle: h, pal_buffer: pb, words, calls_per_s: hz }));
    }
  }});
  console.log('hook en', f);
}
