# Integration checklist

- [x] Standalone optics package, CLI and example config
- [x] Numerical tests and host test script
- [x] Architecture, equations, interface schema and acceptance gates
- [x] Previous audit/closure ZIPs and patch included as reference material
- [ ] Reconcile feature branch with `main` and review the full diff
- [ ] Verify Android Gradle release workflow is green on current head
- [ ] Add JVM unit tests to CI; run native tests + Gradle tests + release assembly
- [ ] Fix measured HRIR onset/ITD alignment and verify via listening/A-B
- [ ] Implement actual SOFA measurement parsing (not signature-only/master-basis reconstruction)
- [ ] Replace placeholder SAF convergence with a measured metric and validate latent mapping
- [ ] Test audio routes on physical Android devices for latency, true peak, dropouts and controls
- [ ] Only consider audio bridge after preregistered A/B demonstrates a benefit
- [ ] Physical optical bench validation before claims of real holographic output

This is integration-ready as a separate research module, not a completed production merge.
