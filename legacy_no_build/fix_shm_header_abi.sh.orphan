#!/data/data/com.termux/files/usr/bin/bash
set -e

python3 - <<'PY'
from pathlib import Path
import re

p = Path("app/src/main/cpp/daemon/core/shm_manager.h")

s = p.read_text()

pattern = r'static_assert\s*\(\s*sizeof\s*\(\s*ShmHeader\s*\)\s*==\s*\d+\s*,.*?\);'

replacement = '''// ABI SHM v2:
// El tamaño real del header depende de la alineación ABI ARM64.
// Kotlin y C++ deben compartir este contrato (32 bytes).
static_assert(sizeof(ShmHeader) == 32,
              "ShmHeader ABI mismatch: Kotlin SHM_HEADER_BYTES debe coincidir");'''

new, count = re.subn(pattern, replacement, s, flags=re.S)

if count == 0:
    print("No se encontró static_assert de ShmHeader.")
    print("Contenido relacionado:")
    for line in s.splitlines():
        if "ShmHeader" in line or "static_assert" in line:
            print(line)
    raise SystemExit(1)

p.write_text(new)

print("static_assert ShmHeader actualizado:", count)
PY

git add app/src/main/cpp/daemon/core/shm_manager.h

git commit -m "fix: align ShmHeader ABI contract with actual ARM64 size"

git push origin main

git rev-parse HEAD
