#!/usr/bin/env python3
"""Validate actual HDF5/SOFA structure and dataset readability; requires h5py."""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

try:
    import h5py
except ImportError as exc:
    raise SystemExit("ERROR: h5py is required; install with python -m pip install h5py") from exc


def validate_file(path: Path) -> list[str]:
    errors: list[str] = []
    try:
        with h5py.File(path, "r") as handle:
            if not handle.keys():
                errors.append("HDF5 file has no root objects")
            convention = handle.attrs.get("Conventions", handle.attrs.get("Convention"))
            if convention is not None:
                if isinstance(convention, bytes):
                    convention = convention.decode("utf-8", "replace")
                if "SOFA" not in str(convention).upper():
                    errors.append(f"unexpected convention attribute: {convention!r}")

            datasets = []
            def collect(name, obj):
                if isinstance(obj, h5py.Dataset):
                    datasets.append((name, obj))
            handle.visititems(collect)
            if not datasets:
                errors.append("HDF5 file contains no datasets")

            # Read all datasets, chunk-by-chunk when chunked, so HDF5 metadata,
            # filters and available checksums are exercised without loading a
            # potentially large impulse-response dataset into RAM at once.
            for name, dataset in datasets:
                try:
                    if dataset.shape == ():
                        dataset[()]
                    elif dataset.size:
                        if dataset.chunks:
                            import math
                            counts = [math.ceil(dim / chunk) for dim, chunk in zip(dataset.shape, dataset.chunks)]
                            for linear in range(math.prod(counts)):
                                remainder = linear
                                slices = []
                                for dim, chunk, count in reversed(list(zip(dataset.shape, dataset.chunks, counts))):
                                    index = remainder % count
                                    remainder //= count
                                    start = index * chunk
                                    slices.append(slice(start, min(start + chunk, dim)))
                                dataset[tuple(reversed(slices))]
                        else:
                            # Bound memory for contiguous datasets by slicing their first axis.
                            if dataset.ndim == 1:
                                step = max(1, min(dataset.shape[0], (8 * 1024 * 1024) // max(1, dataset.dtype.itemsize)))
                                for start in range(0, dataset.shape[0], step):
                                    dataset[start:start + step]
                            else:
                                dataset[...]
                except Exception as exc:
                    errors.append(f"dataset {name!r} unreadable: {type(exc).__name__}: {exc}")
    except Exception as exc:
        errors.append(f"cannot open/parse HDF5: {type(exc).__name__}: {exc}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", nargs="?", type=Path, default=Path("."))
    args = parser.parse_args()
    root = args.root.resolve()
    files = sorted(p for p in root.rglob("*.sofa") if ".git" not in p.parts)
    if not files:
        print(f"[FAIL] no .sofa files found under {root}")
        return 1
    failures = 0
    for path in files:
        issues = validate_file(path)
        if issues:
            failures += 1
            for issue in issues:
                print(f"[FAIL] {path.relative_to(root)}: {issue}")
    print(f"[SOFA-HDF5] files={len(files)} failed={failures} h5py={h5py.__version__}")
    if failures:
        print("[FAIL] SOFA HDF5 structural/readability validation failed")
        return 1
    print("[PASS] Every SOFA opened and all datasets were traversed/read")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
