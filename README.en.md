# IVANNA OMEGA SUPREME — English quick start

> The full documentation is in Spanish ([README.md](README.md), [LEAME.md](LEAME.md)). This page is a short
> orientation for English readers. It does not replace the technical README.

## What it is

An Android audio project: a Kotlin/Jetpack Compose app, a native C++ DSP engine, acoustic measurement tools and
experimental spatial-visualization modules (FotoHolo / AR capture). It is **an engineering project in
development**. Having code for a feature does not mean it is wired into the active audio path, works on every
phone, or has been validated on hardware. The repository separates *what exists in code*, *what CI verifies*
and *what still needs a real device*.

## Two processing routes

| | **Route A — app, no root** | **Route B — root, system-wide · RETIRED** |
|---|---|---|
| Where it runs | Inside the app's own player and capture service | Nowhere. Retired 2026-11-11 (see below) |
| Affects | Audio the app itself plays or captures | Nothing |
| Needs | Just the APK | Do not install any module ZIP |

**Route B is dead.** The HAL wrapper failed on 2026-11-11 (the real HAL binary aborts with SIGABRT when the wrapper `exec`s it; the phone ended in a bootloop and its 6th factory reset). A Magisk module cannot make init read an `.rc` at boot. Do not flash any `ivanna_omega_supreme_vX.Y.Z.zip` — including v2.5.403, which carries the broken wrapper. Route A (APK only) is the only supported route.

The software clock-drift control (PLL) applies to the app's own `AudioTrack` path only. It does **not** control
the physical clock of a USB DAC, a Bluetooth sink or the vendor HAL; the repo documents those as unproven
integration points, not features.

## Install

1. Open the [Releases](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/releases) page and
   take the latest release. Each release carries the APK (the Magisk module ZIP is still built by CI, but
   Route B is retired — do not flash it).
2. Install the APK (Route A). That is the whole install now.

A green CI badge is not proof of sound quality or hardware compatibility. The reference test device is a
Moto G85 on Android 15 (SDK 35); see [the acceptance plan](docs/PLAN_10D_MOTO_G85_ACCEPTANCE.md) for the physical tests
that are still pending. Measured on device 2026-11-11 (v2.5.406, Tidal, all effects on): app at 130% CPU,
0 xruns over USB OTG at 384 kHz/32-bit; blind ABX and temperature still pending.

## Build and test on a computer (no Android SDK needed)

```bash
git clone https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME.git
cd IVANNA-OMEGA-SUPREME
pip install cmake                                  # the native tests need CMake >= 3.30.3
bash scripts/run_ctest.sh                          # builds and runs the host test suite (GoogleTest is vendored)
for g in scripts/check_*.py; do python3 "$g"; done # static quality gates
```

The host suite covers the DSP, the control plane (daemon ↔ snapshot) and the gates. The APK and the Magisk
module are built in CI; Kotlin cannot be compiled without the Android SDK.

## Report a device result

The most valuable contribution is a **tested device report**: open an issue with the *device report* template and
include phone model, Android version, Magisk version (if any), the route used (A or B) and what you actually
observed. Use the *bug report* template for errors, and include the app version (`version.properties`) and
sanitized logs. Please do not post keys, tokens, personal data or private recordings.

Security vulnerabilities must **not** be reported in public issues; use GitHub's private
*Security → Report a vulnerability* flow described in [SECURITY.md](SECURITY.md).

## Project rules in one paragraph

No invented numbers: improvements need an implementation, a measurement and a test, and CI enforces
documentation claims against the code. The audio thread allows no `malloc`, no locks and no I/O. Measured-on-host
with synthetic signals and listened-to-on-device are kept distinct. See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

**IVANNA-OMEGA-SUPREME is proprietary software owned by Luis Uriel Pimentel Pérez. All rights reserved. It is NOT open source.** The repository is public on GitHub (visible), but it has no open-source license: the project's own code may not be reproduced, distributed, modified, or used without the author's express authorization. Third-party components keep their own licenses (see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).
