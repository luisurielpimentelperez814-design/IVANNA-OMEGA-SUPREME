#!/usr/bin/env python3
"""Prepare the Magisk updater manifest from the release version metadata.

The manifest is generated inside the build workspace and published to main only
after the canonical release ZIP and APK have been uploaded and verified.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path


def read_properties(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip()
    return values


def prepare_manifest(
    module_prop: Path,
    version_properties: Path,
    update_json: Path,
    repository: str,
) -> dict[str, object]:
    module = read_properties(module_prop)
    version = read_properties(version_properties)
    module_version = module.get("version", "")
    module_code = module.get("versionCode", "")
    properties_version = version.get("versionName", "")
    properties_code = version.get("versionCode", "")

    if not module_version.startswith("v"):
        raise ValueError("module.prop version must use the v-prefixed release tag")
    if module_version[1:] != properties_version:
        raise ValueError("module.prop and version.properties versionName do not match")
    if not module_code.isdigit() or module_code != properties_code:
        raise ValueError("module.prop and version.properties versionCode do not match")
    if not repository or "/" not in repository:
        raise ValueError("repository must be owner/name")

    current = json.loads(update_json.read_text(encoding="utf-8"))
    current["version"] = module_version
    current["versionCode"] = int(module_code)
    current["zipUrl"] = (
        f"https://github.com/{repository}/releases/download/{module_version}/"
        f"ivanna_omega_supreme_{module_version}.zip"
    )
    current["changelog"] = (
        f"https://raw.githubusercontent.com/{repository}/main/CHANGELOG.md"
    )
    return current


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repository", default=os.environ.get("GITHUB_REPOSITORY", ""))
    parser.add_argument("--module-prop", type=Path, default=Path("magisk_module/module.prop"))
    parser.add_argument("--version-properties", type=Path, default=Path("version.properties"))
    parser.add_argument("--update-json", type=Path, default=Path("magisk_module/update.json"))
    parser.add_argument("--write", action="store_true", help="write the generated manifest")
    args = parser.parse_args()

    manifest = prepare_manifest(
        args.module_prop, args.version_properties, args.update_json, args.repository
    )
    rendered = json.dumps(manifest, indent=2, ensure_ascii=False) + "\n"
    if args.write:
        args.update_json.write_text(rendered, encoding="utf-8")
    else:
        print(rendered, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
