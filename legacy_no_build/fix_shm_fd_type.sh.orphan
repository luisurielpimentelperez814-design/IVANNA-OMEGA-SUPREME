#!/bin/bash
set -e

FILE="app/src/main/java/com/ivanna/omega/magisk/ShmManager.kt"

python3 - <<'PY'
from pathlib import Path

p = Path("app/src/main/java/com/ivanna/omega/magisk/ShmManager.kt")
s = p.read_text()

old = """private external fun nativeMapSharedFd(fd: Int, size: Int): ByteBuffer?"""

new = """private external fun nativeMapSharedFd(fd: FileDescriptor, size: Int): ByteBuffer?"""

if old in s:
    s = s.replace(old,new)
else:
    print("firma ya cambiada o no encontrada")

old2 = """val buf = nativeMapSharedFd(rawFd, realSize)"""

new2 = """val nativeFd = ParcelFileDescriptor.adoptFd(rawFd)
            val buf = nativeMapSharedFd(nativeFd.fileDescriptor, realSize)"""

if old2 in s:
    s = s.replace(old2,new2)
else:
    print("llamada no encontrada")

p.write_text(s)
PY

git add app/src/main/java/com/ivanna/omega/magisk/ShmManager.kt

git commit -m "fix: align nativeMapSharedFd Kotlin signature with FileDescriptor JNI contract"

git pull --rebase origin main

git push origin main

git rev-parse HEAD

echo "FIX SHM FILEDESCRIPTOR APLICADO"
