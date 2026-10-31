#!/usr/bin/env python3
"""Fail-closed ELF gate for the Android Audio EffectsFactory AELI entry point.

Checks the built libomega_effect.so dynamic symbol table, not source text or
the ordinary .symtab. The expected ABI is one defined STT_OBJECT symbol with
GLOBAL binding and DEFAULT visibility so dlsym(handle, "AELI") can resolve it.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys


SYMBOL_RE = re.compile(
    r"^\s*\d+:\s+\S+\s+\d+\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(.+?)\s*$"
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("elf", help="built libomega_effect.so")
    parser.add_argument("--readelf", default="readelf", help="GNU readelf or llvm-readelf")
    args = parser.parse_args()

    try:
        result = subprocess.run(
            [args.readelf, "--dyn-syms", "--wide", args.elf],
            check=True, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        print(f"ERROR: unable to inspect ELF dynamic symbols: {exc}", file=sys.stderr)
        return 2

    matches = []
    for line in result.stdout.splitlines():
        match = SYMBOL_RE.match(line)
        if not match:
            continue
        symbol_type, binding, visibility, section_index, name = match.groups()
        if name.split("@", 1)[0] == "AELI":
            matches.append((symbol_type, binding, visibility, section_index, name))

    if len(matches) != 1:
        print(f"ERROR: expected exactly one AELI entry in .dynsym; found {len(matches)}", file=sys.stderr)
        return 1

    symbol_type, binding, visibility, section_index, name = matches[0]
    failures = []
    if symbol_type != "OBJECT":
        failures.append(f"type={symbol_type}, expected OBJECT")
    if binding != "GLOBAL":
        failures.append(f"binding={binding}, expected GLOBAL")
    if visibility != "DEFAULT":
        failures.append(f"visibility={visibility}, expected DEFAULT")
    if section_index == "UND":
        failures.append("symbol is undefined (Ndx=UND)")
    if failures:
        print("ERROR: invalid AELI dynamic export: " + "; ".join(failures), file=sys.stderr)
        return 1

    print(f"PASS: .dynsym exports {name} as defined GLOBAL DEFAULT OBJECT")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
