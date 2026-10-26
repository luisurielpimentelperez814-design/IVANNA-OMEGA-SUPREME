# IVANNA Auditory Magnificence Gate

The PCM analyzer now reports severity-coded findings and concrete adjustments for clipping, peak headroom, DC offset, channel imbalance, short captures, known-tone THD+N residual and reference alignment/SNR. Thresholds are conservative screening heuristics, not universal perceptual standards. A clean synthetic WAV proves only that the measurement code behaves as tested; it does not certify a phone, Bluetooth receiver, DAC or acoustic chain.

Run adversarial tests with `python3 -m unittest discover -s tests -p 'test_ivanna_audio_certify.py' -v`. The test set includes clean tone, full-scale clipping, channel imbalance, DC offset, reference length mismatch, JSON finiteness, 24-bit PCM sign extension, sample-rate mismatch and invalid tone frequency. Hardware GREEN still requires captured physical-route evidence and level-matched listening evidence.
