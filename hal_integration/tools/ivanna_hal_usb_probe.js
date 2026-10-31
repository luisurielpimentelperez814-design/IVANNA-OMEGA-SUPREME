const { execSync } = require('child_process');
function sh(c){try{return execSync(c,{encoding:'utf8'});}catch(e){return "ERR "+e.message;}}
console.log("=== USB-C AudioOut_15 handle 21 ===");
console.log(new Date().toISOString());
console.log(sh("su -c 'cat /data/local/tmp/ivanna_audio_runtime.txt' | grep -E 'AudioOut_15|USB_HEADSET|21|localSR|rate' | tail -20"));
console.log(sh("su -c 'dumpsys media.audio_flinger' | grep -A5 -B5 'USB' | tail -40"));
console.log(sh("su -c 'strings /vendor/lib64/hw/audio.primary.blair.so' | grep -E 'GetMmapPosition|pal_stream_get_mmap_position' | head -10"));
for(let i=0;i<3;i++){ console.log("--- USB Muestra "+(i+1)+" ---"); console.log(sh("su -c 'cat /data/local/tmp/ivanna_audio_runtime.txt' | grep 21 | tail -5")); execSync("sleep 2"); }
