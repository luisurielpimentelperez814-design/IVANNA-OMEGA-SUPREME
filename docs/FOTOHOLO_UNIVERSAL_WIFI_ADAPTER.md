# Universal Wi-Fi sensing adapter contract

## Goal and boundary

This adapter provides one stable API across device families and honest fallback behavior. It does not claim that arbitrary Android Wi-Fi chipsets expose CSI. Standard Wi-Fi scans generally provide access-point metadata and RSSI, not per-subcarrier channel measurements.

## Layers

1. Android scan provider: platform WifiManager results, subject to Android permissions, location settings and scan throttling.
2. Root survey provider: Magisk/root plus compatible iw can request ordinary scan results if the driver permits it. Root is not equivalent to CSI access.
3. Verified CSI provider: an explicit, hardware-specific implementation, validated against real CSI samples on the target chipset/driver/firmware/kernel. It reports sample format, antennas, subcarriers, timestamps, rate and error state.
4. Common feature API: source, timestamp, freshness, confidence, signal variability and explicit limitations.
5. Acoustic consumer: may use only validated room/acoustic measurements to adapt DSP. RSSI changes alone must not change EQ, room geometry, volume or spatial rendering automatically.

## Privacy and product behavior

- No scan before explicit Wi-Fi-sensing opt-in.
- Visible status while running; stop on background and revocation.
- Process locally by default and avoid storing BSSID/SSID or raw CSI beyond the session unless separately opted in.
- Do not claim exact people count, anatomy, identity, or room geometry from ordinary RSSI.
- For occupancy/activity models, validate across different rooms, devices, router placement and time. Abstain when confidence is insufficient.

## Why an app-only universal CSI driver is not possible

CSI is generated and exposed by the Wi-Fi chipset/firmware/driver path. A generic Android app cannot manufacture absent subcarrier measurements. A chipset-specific driver or external CSI-capable receiver may be required. The universal layer is therefore a plugin contract plus fallbacks, not a fictional universal low-level driver.

## Research links

- Android Wi-Fi scanning and throttling: https://developer.android.com/develop/connectivity/wifi/wifi-scan
- WifiManager API: https://developer.android.com/reference/android/net/wifi/WifiManager
- Temporal generalization limits for CSI sensing (2025): https://doi.org/10.1016/j.iot.2025.101634
- CSI-based human pose estimation (2025): https://doi.org/10.3390/electronics14040756
- Cross-domain generalization review: https://doi.org/10.1007/s10462-024-10812-4

## Acceptance gate

Do not mark a chipset as CSI-capable until an adapter has captured real CSI and passed repeatability, timestamp, packet-loss, environmental-shift and device-specific tests. Keep the CSI flag false otherwise.


## Implemented extension points

- `UniversalWifiSensingAdapter.startCsiSession(...)` starts only a registered, explicitly verified provider supported by the current device, then forwards captured frames to `CsiFeatureExtractor`.
- `CsiFeatureExtractor` computes amplitude mean/dispersion, median absolute deviation, circular phase coherence, temporal amplitude change and a data-quality score from actual CSI frames. It never synthesizes CSI from RSSI.
- `WifiSceneReconstruction.evaluate(...)` is a fail-closed gate. A future 3D reconstruction pipeline cannot report itself ready without a device-verified CSI provider, calibration ID, at least four known non-coplanar anchors, synchronized links, a minimum frame set, calibrated range/angle estimates and a versioned model.
- JVM unit tests cover CSI feature statistics, temporal deltas, invalid frames and reconstruction readiness gates.

## What is still not implemented or proven

No chipset-specific CSI provider is registered by these extension points yet. Consequently, `startCsiSession` correctly returns unavailable on current builds until a real provider is implemented and verified. The reconstruction gate is not itself a 3D reconstruction algorithm; room dimensions, wall materials, person count and anatomy are not produced by the current scan path. Those require a calibrated measurement setup, datasets, model implementation and physical validation. No device-level accuracy or real-time performance is claimed.
