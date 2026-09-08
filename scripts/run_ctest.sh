#!/bin/bash
# Puerta de tests host nativos — compila y ejecuta la suite C++ con los
# include/link correctos (GTest vendored, spatial/, adaptive_engine real).
# Uso: bash scripts/run_ctest.sh [--verbose]
set -uo pipefail
CPP="app/src/main/cpp"
GT="$CPP/tests/third_party/googletest"
FAIL=0
run() { echo "── $1"; "$@"; }

echo "== Build GTest vendored =="
g++ -std=c++17 -O2 -I"$GT/googletest/include" -I"$GT/googlemock/include" -c "$GT/googletest/src/gtest-all.cc" -o /tmp/ivanna_gtest-all.o || FAIL=1
g++ -std=c++17 -O2 -I"$GT/googletest/include" -I"$GT/googlemock/include" -c "$GT/googlemock/src/gmock-all.cc" -o /tmp/ivanna_gmock-all.o || FAIL=1
ar rcs /tmp/ivanna_libgtest.a /tmp/ivanna_gtest-all.o /tmp/ivanna_gmock-all.o || FAIL=1

echo "== test_ihr1_format =="
run g++ -std=c++17 -O2 -I"$CPP" -o /tmp/ivanna_ihr1 tests/hrtf/test_ihr1_format.cpp || FAIL=1
[ -x /tmp/ivanna_ihr1 ] && run timeout 30 /tmp/ivanna_ihr1 || FAIL=1

echo "== gammatone_numerical_stability =="
run g++ -std=c++17 -O2 -I"$CPP" -I"$GT/googletest/include" -I"$GT/googlemock/include" \
    "$CPP/tests/gammatone_numerical_stability.cpp" /tmp/ivanna_libgtest.a -lpthread -o /tmp/ivanna_gamma || FAIL=1
[ -x /tmp/ivanna_gamma ] && run timeout 30 /tmp/ivanna_gamma || FAIL=1

echo "== regression/test_oem_stability_suite =="
run g++ -std=c++17 -O2 -I"$CPP" -I"$GT/googletest/include" -I"$GT/googlemock/include" \
    "$CPP/tests/regression/test_oem_stability_suite.cpp" /tmp/ivanna_libgtest.a -lpthread -o /tmp/ivanna_oem || FAIL=1
[ -x /tmp/ivanna_oem ] && run timeout 40 /tmp/ivanna_oem || FAIL=1

echo "== adaptive_engine (test_stability, test_close_loop) =="
AE_KERNEL="$CPP/experimental/adaptive_engine/adaptive_decision_engine.cpp"
if [ -f "$AE_KERNEL" ]; then
  for t in test_stability test_close_loop; do
    run g++ -std=c++17 -O2 -I"$CPP" -I"$CPP/experimental" -I"$CPP/experimental/adaptive_engine" \
        "$CPP/experimental/adaptive_engine/tests/$t.cpp" "$AE_KERNEL" -pthread -o "/tmp/ivanna_$t" || FAIL=1
    [ -x "/tmp/ivanna_$t" ] && run timeout 30 "/tmp/ivanna_$t" || FAIL=1
  done
else
  echo "AVISO: kernel adaptive_decision_engine.cpp no hallado — tests adaptive omitidos (FAIL)"
  FAIL=1
fi

if [ "$FAIL" -eq 0 ]; then echo "CTEST HOST: TODOS LOS TESTS OK"; else echo "CTEST HOST: HUBO FALLOS"; fi
exit "$FAIL"
