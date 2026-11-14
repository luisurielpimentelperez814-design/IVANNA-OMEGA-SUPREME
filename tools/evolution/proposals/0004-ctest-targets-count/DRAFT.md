# Draft 0004-ctest-targets-count (LLM vía OmniRoute)

- **Modelo:** ivanna-free (gateway http://localhost:20128/v1)
- **Latencia de redacción:** 79.6 s
- **Hallazgo:** repo_state_undercount: repo_state() cuenta 36 targets CTest cuando hay ~104
- **Archivos del parche:** tools/evolution/evolution_engine.py, tools/evolution/tests/test_evolution_engine.py

## Respuesta completa del LLM

```
```diff
diff --git a/tools/evolution/evolution_engine.py b/tools/evolution/evolution_engine.py
index 0000000..0000000 100644
--- a/tools/evolution/evolution_engine.py
+++ b/tools/evolution/evolution_engine.py
@@ -1,6 +1,7 @@
 #!/usr/bin/env python3
 """
 Motor de Auto-Evolución en Código de IVANNA — MVP (2026-11-09).
@@ -27,6 +28,7 @@ import json
 import os
 import re
 import shutil
+import shlex
 import subprocess
 import sys
 import tempfile
@@ -78,6 +80,84 @@ def is_forbidden(rel: str) -> bool:
             or "PAT" in rel)
 
 
+def _strip_cmake_comments(text: str) -> str:
+    """Remove # comments while preserving string contents enough for regex scans."""
+    out = []
+    for line in text.splitlines():
+        buf = []
+        in_quote = False
+        escaped = False
+        for ch in line:
+            if escaped:
+                buf.append(ch)
+                escaped = False
+                continue
+            if ch == "\\":
+                buf.append(ch)
+                escaped = True
+                continue
+            if ch == '"':
+                in_quote = not in_quote
+                buf.append(ch)
+                continue
+            if ch == "#" and not in_quote:
+                break
+            buf.append(ch)
+        out.append("".join(buf))
+    return "\n".join(out)
+
+
+def _count_ctest_targets(cmake_text: str) -> int:
+    """Count CTest registrations more accurately than raw add_test(NAME ...).
+
+    The repo's test CMake uses a helper wrapper:
+      ivanna_add_test(name) -> add_test(NAME ...)/gtest_discover_tests(...)
+
+    so counting only direct add_test(NAME ...) undercounts registered test
+    targets by roughly the number of helper invocations. This parser:
+      - counts direct add_test(NAME ...)
+      - counts helper-driven registrations via ivanna_add_test(...)
+      - ignores the helper definition itself
+      - treats gtest_discover_tests(...) as a CTest registration hook only
+        when attached to an ivanna_add_test definition context, avoiding
+        double-counting the same target.
+    """
+    text = _strip_cmake_comments(cmake_text)
+
+    def_calls = set(re.findall(r"\bivanna_add_test\s*\(\s*([A-Za-z0-9_./:-]+)", text))
+    direct = set(re.findall(r"\badd_test\s*\(\s*NAME\s+([A-Za-z0-9_./:-]+)", text, flags=re.IGNORECASE))
+
+    helper_invocations = set()
+    for m in re.finditer(r"\bivanna_add_test\s*\(\s*([A-Za-z0-9_./:-]+)", text):
+        name = m.group(1)
+        if name in {"NAME", "COMMAND"}:
+            continue
+        helper_invocations.add(name)
+
+    # If the file uses a helper and direct registrations, count unique targets.
+    # This matches the intent of repo_state(): number of CTest-registered test
+    # binaries/targets visible to ctest, not merely raw add_test() calls.
+    return len(direct | helper_invocations | def_calls)
+
+
 # ── Fase 1: estado ──────────────────────────────────────────────────────────
 def repo_state():
     log = run(["git", "log", "--oneline", "-5"], check=True).stdout.strip()
     status = run(["git", "status", "--short"]).stdout.strip()
-    n_tests = len(re.findall(r"add_test\(NAME", (REPO / "app/src/main/cpp/tests/CMakeLists.txt").read_text()))
+    cmake_text = (REPO / "app/src/main/cpp/tests/CMakeLists.txt").read_text(errors="ignore")
+    n_tests = _count_ctest_targets(cmake_text)
     return {"head": log.splitlines(), "dirty": status.splitlines(), "ctest_targets": n_tests}
@@ -451,3 +531,90 @@ def main():
 if __name__ == "__main__":
     raise SystemExit(main())
+
diff --git a/tools/evolution/tests/test_evolution_engine.py b/tools/evolution/tests/test_evolution_engine.py
new file mode 100644
index 0000000..0000000
--- /dev/null
+++ b/tools/evolution/tests/test_evolution_engine.py
@@ -0,0 +1,90 @@
+import importlib.util
+from pathlib import Path
+
+
+def _load_engine():
+    root = Path(__file__).resolve().parents[2]
+    path = root / "tools" / "evolution" / "evolution_engine.py"
+    spec = importlib.util.spec_from_file_location("evolution_engine", path)
+    mod = importlib.util.module_from_spec(spec)
+    assert spec is not None and spec.loader is not None
+    spec.loader.exec_module(mod)
+    return mod
+
+
+def test_count_ctest_targets_counts_helper_invocations_and_direct_add_tests():
+    engine = _load_engine()
+    cmake = """
+    function(ivanna_add_test name)
+      add_test(NAME ${name} COMMAND $<TARGET_FILE:${name}>)
+      gtest_discover_tests(${name})
+    endfunction()
+
+    ivanna_add_test(foo)
+    ivanna_add_test(bar)
+    add_test(NAME direct_one COMMAND direct_one)
+    add_test(NAME direct_two COMMAND direct_two)
+    """
+
+    assert engine._count_ctest_targets(cmake) == 4
+
+
+def test_count_ctest_targets_ignores_helper_definition_and_comments():
+    engine = _load_engine()
+    cmake = """
+    # ivanna_add_test(fake)
+    function(ivanna_add_test name)
+      # add_test(NAME commented COMMAND ignored)
+      gtest_discover_tests(${name})
+    endfunction()
+    """
+
+    assert engine._count_ctest_targets(cmake) == 0
+
```
```
