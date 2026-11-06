#!/bin/sh
# Host test: the probe must be transparent (return values / buffer untouched) and count correctly.
set -eu
D=$(mktemp -d); trap 'rm -rf "$D"' EXIT
H=$(cd "$(dirname "$0")" && pwd); S=$H/..
CC=${CC:-cc}
$CC -std=c11 -O2 -fPIC -Wall -Wextra -Werror -shared "$H/fake_pal.c" -o "$D/libfakepal.so"
$CC -std=c11 -O2 -Wall -Wextra -Werror "$H/caller.c" -L"$D" -lfakepal -Wl,-rpath,"$D" -o "$D/caller"
$CC -std=gnu11 -O2 -fPIC -fvisibility=hidden -Wall -Wextra -Werror -shared -DIVANNA_OUT_PATH="\"$D/out.txt\"" -DIVANNA_OFF_PATH="\"$D/off\"" \
    -DIVANNA_DUMP_PERIOD_S=1 "$S/ivanna_pal_write_counter.c" -o "$D/probe.so" -ldl -lpthread
LD_PRELOAD="$D/probe.so" "$D/caller"
cat "$D/out.txt"
grep -q "total_calls=1502" "$D/out.txt"
grep -Eq "calls=1002 bytes=768060 min_size=13 max_size=768 last_size=100 short=1 err=1" "$D/out.txt"
grep -Eq "calls=500 bytes=768000 min_size=1536 max_size=1536 last_size=1536 short=0 err=0" "$D/out.txt"
echo "host test: PASS"
nm -D "$D/probe.so" | grep -q " T pal_stream_write" && echo "symbol exported: PASS"
