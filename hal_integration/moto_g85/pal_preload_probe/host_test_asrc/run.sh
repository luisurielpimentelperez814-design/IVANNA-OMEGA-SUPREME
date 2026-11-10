#!/bin/sh
# Host test del write-stage ASRC: el shim debe ser transparente en los
# caminos de passthrough, resamplear el stream de musica (attr 48k/16/2 y
# datos con w14..w17 en cero por formato CONOCIDO), pasar la voz sin
# tocarla, y caer al fallback de write-timing (sin libar-pal.so en host).
# Espejo de host_test_attr/run.sh. Tambien corre el unit test del
# resampler / adaptador mmap / servo.
set -eu
D=$(mktemp -d); trap 'rm -rf "$D"' EXIT
H=$(cd "$(dirname "$0")" && pwd); S=$H/..
CC=${CC:-cc}
$CC -std=c11 -O2 -fPIC -Wall -Wextra -Werror -shared "$H/fake_pal_asrc.c" -o "$D/libfakeasrc.so"
$CC -std=c11 -O2 -Wall -Wextra -Werror "$H/caller_asrc.c" -L"$D" -lfakeasrc -Wl,-rpath,"$D" -o "$D/caller_asrc"
# Shim para host: LOGI es no-op (sin -D__ANDROID__), salida a $D/out.txt.
$CC -std=gnu11 -O2 -fPIC -fvisibility=hidden -Wall -Wextra -Werror -shared -DIVANNA_OUT_PATH="\"$D/out.txt\"" -DIVANNA_OFF_PATH="\"$D/off\"" \
    -DIVANNA_DUMP_PERIOD_S=1 "$S/ivanna_asrc_write_stage.c" -o "$D/stage.so" -ldl -lpthread
# Unit test (resampler / mmap_validate / servo_update).
# -lm: the v5 test measures frequency response / THD+N (sin, cos,
# sqrt, log10) with a least-squares sine fit.
$CC -std=gnu11 -O2 -Wall -Wextra -Werror -DIVANNA_ASRC_UNIT_TEST -DIVANNA_OUT_PATH="\"$D/unit_out.txt\"" -DIVANNA_OFF_PATH="\"$D/off\"" \
    "$H/unit_asrc.c" -o "$D/unit_asrc" -ldl -lpthread -lm
"$D/unit_asrc"
LD_PRELOAD="$D/stage.so" "$D/caller_asrc"
cat "$D/out.txt"
# Cabecera: 4 opens ok, 1 close, sin fallos de resolucion ni de tabla;
# 1 passthrough por formato (voz), 2 por tamano (770 y 13 misaligned);
# reloj = write-timing (en host no hay libar-pal.so que resolver).
grep -q "opens=4 ok=4 fail=0 closes=1 open_no_real=0 close_no_real=0 write_no_real=0 bad_attr=0 no_handle=0 table_full=0 pt_no_slot=0 pt_fmt=1 pt_size=2 pt_off=0 pt_nobuf=0 pt_big=0 max_write_frames=192" "$D/out.txt"
grep -q "ratio=0.999993750 drift_ppm=-6.250 clock=write-timing" "$D/out.txt"
# Musica (h=0x1000, attr 48k/16/2): 14 writes, 9331 bytes, 12 procesados,
# 2 passthrough (misaligned), in_frames=2137. out_frames es ~in_frames
# (ratio ~1.0) mas los 192 del passthrough de 770 B; se acepta 2xxx.
grep -Eq "STREAM h=0x1000 fmt=48000/16/ch2/fmt1 src=attr eligible=1 writes=14 bytes=9331 processed=12 passthrough=2 misalign=2 in_frames=2137 out_frames=2[0-9]{3} pend=0" "$D/out.txt"
# Datos de musica (h=0x3000, w14..w17 en cero): elegible por CONOCIDO,
# 5 writes antes del close de voz + 1 write DESPUES (la regresion del
# tombstone: con el bug viejo ese ultimo write era pt_no_slot y el total
# quedaba en 5). in_frames=1152.
grep -Eq "STREAM h=0x3000 fmt=0/0/ch0/fmt0 src=known eligible=1 writes=6 bytes=4608 processed=6 passthrough=0 misalign=0 in_frames=1152 out_frames=11[0-9]{2} pend=0" "$D/out.txt"
# Voz (h=0x2000, cerrada) NO debe aparecer en el dump.
! grep -q "h=0x2000" "$D/out.txt"
# Hi-res (h=0x4000, 192k/16/2): elegible en v5, 3 writes de 768 B
# (576 frames in, ~576 out a ratio ~1.0).
grep -Eq "STREAM h=0x4000 fmt=192000/16/ch2/fmt1 src=attr eligible=1 writes=3 bytes=2304 processed=3 passthrough=0 misalign=0 in_frames=576 out_frames=57[0-9] pend=0" "$D/out.txt"
echo "host test asrc write stage: PASS"
nm -D "$D/stage.so" | grep -q " T pal_stream_open" && nm -D "$D/stage.so" | grep -q " T pal_stream_close" && nm -D "$D/stage.so" | grep -q " T pal_stream_write" && echo "symbols exported: PASS"
# dlinfo must NOT appear anywhere in the host shim (HAL link failure,
# crash-loop 2026-11-09). The base comes from /proc/self/maps instead.
! nm -D "$D/stage.so" | grep -q "dlinfo" && echo "no dlinfo: PASS"
