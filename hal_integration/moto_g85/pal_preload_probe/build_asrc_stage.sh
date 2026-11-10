#!/bin/sh
# Build del write-stage ASRC PAL para AArch64, SIN NDK.
# Misma receta que la sonda de atributos (ver build_attr_probe.sh):
#   aarch64-linux-gnu-gcc 13.3, -nostdlib, -march=armv8-a+lse,
#   -D_FORTIFY_SOURCE=0, stub <android/log.h>.
# La .so resultante solo necesita libdl.so + liblog.so (bionic); el resto
# de simbolos (snprintf, open, read, pthread_create, malloc, ...) quedan
# sin resolver para enlazarse en runtime contra la libc ya cargada en el
# HAL. dlinfo esta PROHIBIDO: no existe en el namespace del linker vendor
# del HAL (el HAL falla con "cannot locate symbol dlinfo" y entra en
# crash-loop, 2026-11-09); la base de libar-pal.so se lee de /proc/self/maps.
# Uso: sh build_asrc_stage.sh   (desde este directorio)
# Salida: out/libivanna_asrc_write_stage.so + out/sha256.txt + out/dynsyms.txt
set -eu
H=$(cd "$(dirname "$0")" && pwd)
OUT="$H/out"
CC=${CC:-aarch64-linux-gnu-gcc}
READELF=${READELF:-aarch64-linux-gnu-readelf}
mkdir -p "$OUT" "$H/build_stub/lib"

# Stub libs: .so vacios solo para que el linker registre DT_NEEDED
# libdl.so / liblog.so (los simbolos reales se resuelven en runtime).
"$CC" -shared -fPIC -o "$H/build_stub/lib/libdl.so" "$H/build_stub/empty.c"
"$CC" -shared -fPIC -o "$H/build_stub/lib/liblog.so" "$H/build_stub/empty.c"

"$CC" -std=gnu11 -O2 -fPIC -fvisibility=hidden -Wall -Wextra -Werror \
    -nostdlib -march=armv8-a+lse -D_FORTIFY_SOURCE=0 -D__ANDROID__ \
    -I "$H/build_stub" \
    "$H/ivanna_asrc_write_stage.c" -shared \
    -o "$OUT/libivanna_asrc_write_stage.so" \
    -Wl,--no-as-needed -L "$H/build_stub/lib" -ldl -llog

"$READELF" -h "$OUT/libivanna_asrc_write_stage.so" | grep -q AArch64
"$READELF" --dyn-syms -W "$OUT/libivanna_asrc_write_stage.so" | grep -E "pal_stream_(open|close|write)" | tee "$OUT/dynsyms.txt"
# DT_NEEDED libdl.so + liblog.so son ESENCIALES: sin ellos el linker de
# Android no carga esas libs y dlopen/dlsym/__android_log_print no
# se resuelven. (--no-as-needed porque los stubs estan vacios y el linker
# con --as-needed descartaria los NEEDED.)
NEEDED=$("$READELF" -d "$OUT/libivanna_asrc_write_stage.so" | grep NEEDED || true)
printf '%s\n' "$NEEDED" | tee "$OUT/needed.txt"
printf '%s\n' "$NEEDED" | grep -q "libdl.so" || { echo "missing NEEDED libdl.so"; exit 1; }
printf '%s\n' "$NEEDED" | grep -q "liblog.so" || { echo "missing NEEDED liblog.so"; exit 1; }
# El log real debe estar activo (-D__ANDROID__): sin el, LOGI es no-op.
"$READELF" --dyn-syms -W "$OUT/libivanna_asrc_write_stage.so" | grep -q "__android_log_print" || { echo "missing __android_log_print (falta -D__ANDROID__?)"; exit 1; }
# dlinfo esta PROHIBIDO (no existe en el namespace vendor del HAL;
# 2026-11-09). La base de libar-pal.so se lee de /proc/self/maps.
# Se vuelcan los imports dinamicos a und_syms.txt para revision y se
# falla si dlinfo aparece en cualquier forma.
"$READELF" --dyn-syms -W "$OUT/libivanna_asrc_write_stage.so" | grep -E " UND " | awk '$NF != "UND" {print $NF}' | sort -u | tee "$OUT/und_syms.txt"
if grep -q "dlinfo" "$OUT/und_syms.txt"; then echo "forbidden UND dlinfo"; exit 1; fi
# malloc/free (scratch por slot) deben quedar como imports de libc,
# no como stubs locales.
"$READELF" --dyn-syms -W "$OUT/libivanna_asrc_write_stage.so" | grep -q " UND malloc" || { echo "missing UND malloc"; exit 1; }
# El constructor necesita .init_array.
"$READELF" -d "$OUT/libivanna_asrc_write_stage.so" | grep -q INIT_ARRAY || { echo "missing .init_array"; exit 1; }
# No debe importar simbolos pal_* (serian llamadas a la real sin resolver)
if "$READELF" --dyn-syms -W "$OUT/libivanna_asrc_write_stage.so" | grep -E " UND " | grep -E "pal_"; then
    echo "unexpected pal_* import"; exit 1
fi
# No debe haber simbolos versionados glibc, fortify ni libm (el shim no
# usa floor/lrint: el redondeo es manual).
if "$READELF" --dyn-syms -W "$OUT/libivanna_asrc_write_stage.so" | grep -Eq "__aarch64_|__errno_location|__snprintf_chk|__memcpy_chk|floor|lrint|libm"; then
    echo "unexpected glibc/fortify/libm symbol"; exit 1
fi
sha256sum "$OUT/libivanna_asrc_write_stage.so" | tee "$OUT/sha256.txt"
echo "build_asrc_stage: PASS"
