IVANNA OMEGA PAL PRESENCE PROBE — Moto G85
Diagnostic-only module. It does not connect ASRC or intercept PCM.

Library:
  /vendor/lib64/libivanna_pal_presence_probe.so

Add inside the verified target service stanza, before exec:
  setenv LD_PRELOAD /vendor/lib64/libivanna_pal_presence_probe.so

Do not overwrite an existing LD_PRELOAD value; append with ':' if needed.
Do not guess the service stanza. Back it up first. Reboot to apply.

Report:
  /data/vendor/ivanna_audio/pal_preload_presence.txt

Rollback: remove the added init entry, disable the module in Magisk, reboot.

This probe logs process identity, matching loaded objects and PAL symbol
visibility only. It does not hook pal_stream_write, generate timestamps or run ASRC.
