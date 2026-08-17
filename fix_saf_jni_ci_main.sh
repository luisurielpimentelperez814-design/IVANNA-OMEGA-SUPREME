#!/data/data/com.termux/files/usr/bin/bash
set -e

echo "[1/6] Backup"
mkdir -p ~/saf_backup_final

cp app/src/main/cpp/jni/ivanna_omega_jni.cpp ~/saf_backup_final/
cp app/src/main/cpp/include/omega_shared.h ~/saf_backup_final/
cp app/src/main/cpp/include/saf_math_engine.h ~/saf_backup_final/

echo "[2/6] Fix JNI namespace"

sed -i 's/^namespace ivanna;$/using namespace ivanna;/' \
app/src/main/cpp/jni/ivanna_omega_jni.cpp


echo "[3/6] Remove duplicated SAFState"

sed -i '/static SAFState g_saf_state;/d' \
app/src/main/cpp/jni/ivanna_omega_jni.cpp


echo "[4/6] Protect saf_math_engine"

grep -q "#pragma once" app/src/main/cpp/include/saf_math_engine.h || \
sed -i '1i#pragma once' app/src/main/cpp/include/saf_math_engine.h


echo "[5/6] Check missing runtime members"

grep -q "ai_runtime_comp_amount" app/src/main/cpp/include/omega_shared.h || \
sed -i '/ai_runtime_spatial_width;/a\
    std::atomic<float> ai_runtime_comp_amount;\
    std::atomic<float> ai_runtime_exciter_red;' \
app/src/main/cpp/include/omega_shared.h


echo "[6/6] Verify"

grep -R "namespace ivanna;\|static SAFState g_saf_state\|SAFState g_saf_state" \
app/src/main/cpp/jni app/src/main/cpp/include app/src/main/cpp/*.cpp || true


git add \
app/src/main/cpp/jni/ivanna_omega_jni.cpp \
app/src/main/cpp/include/omega_shared.h \
app/src/main/cpp/include/saf_math_engine.h

git commit -m "fix(ci): resolve SAF namespace linkage and duplicate state"

echo "DONE"
