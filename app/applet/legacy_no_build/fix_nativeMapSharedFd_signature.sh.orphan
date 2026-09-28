#!/bin/bash
set -e

echo "Buscando implementación JNI nativeMapSharedFd..."

FILE=$(grep -rl "nativeMapSharedFd" app/src/main/cpp | grep -E "\.(cpp|c)$" | head -1)

if [ -z "$FILE" ]; then
    echo "ERROR: no se encontró implementación JNI"
    exit 1
fi

echo "Archivo encontrado: $FILE"

python3 - <<PY
from pathlib import Path

p = Path("$FILE")
s = p.read_text()

old_patterns = [
"""jobject fileDescriptor,
    jint size""",
"""jobject pfd,
    jint size""",
"""jobject fd,
    jint size"""
]

changed = False

for old in old_patterns:
    if old in s:
        new = """jint fd,
    jint size"""
        s = s.replace(old, new)
        changed = True

# Sustituir extracción Java FileDescriptor si existe
repls = [
(
"""int rawFd = env->GetIntField(fd, fdField);""",
"""int rawFd = static_cast<int>(fd);"""
),
(
"""int rawFd = env->GetIntField(fileDescriptor, fdField);""",
"""int rawFd = static_cast<int>(fd);"""
),
(
"""int rawFd = env->GetIntField(pfd, fdField);""",
"""int rawFd = static_cast<int>(fd);"""
)
]

for a,b in repls:
    if a in s:
        s=s.replace(a,b)
        changed=True

if not changed:
    print("AVISO: no hubo reemplazos automáticos")
else:
    p.write_text(s)
    print("JNI corregido")

PY

echo "Verificando firma..."

grep -n "nativeMapSharedFd" "$FILE"

git add "$FILE"

git commit -m "fix: align nativeMapSharedFd JNI signature with Kotlin fd Int"

git pull --rebase origin main

git push origin main

git rev-parse HEAD

echo "FIX nativeMapSharedFd JNI APLICADO"
