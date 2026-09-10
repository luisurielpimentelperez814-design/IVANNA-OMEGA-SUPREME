# 🔧 Coordinación → flanco Daemon/Magisk: UUID de omega_effect roto en 5 XML

**De:** flanco Integración AudioFlinger (sesión Genspark, 2026-09-10)
**Para:** quien tenga tomado el flanco Daemon nativo + runtime del módulo Magisk
**Prioridad:** CRÍTICA — sin este fix el DSP queda registrado pero inerte en dispositivo.

## El bug (con evidencia)

Todos los `audio_effects*.xml` bajo `magisk_module/` registran el efecto con:

```
uuid="8d7d5e0a-a6eb-4fde-a0ff-cb1b2dd7275e"
```

Pero el binario nativo y la app usan OTRO UUID — fuente de verdad:

- `app/src/main/cpp/omega_effect.cpp:135`:
  ```c
  static const effect_uuid_t OMEGA_EFFECT_UUID = {
      0x4956414e, 0x4e41, 0x4f4d, 0x4547, {0x41, 0x53, 0x55, 0x50, 0x52, 0x45}
  };
  ```
  (= ASCII `"IVANNAOMEGASUPRE"` → UUID textual `4956414e-4e41-4f4d-4547-415355505245`)
- `app/src/main/java/com/ivanna/omega/audio/IvannaGlobalEffectManager.kt:233`:
  `UUID.fromString("4956414e-4e41-4f4d-4547-415355505245")` — con comentario en
  la línea 231 que YA advertía: "auditorías externas (8d7d5e0a-...) — no coincide
  con el binario".

**Consecuencia:** AudioFlinger resuelve efectos por UUID. Con la discrepancia,
`AudioEffect`/`EffectCreate` falla con `-EINVAL` y ninguna muestra de audio pasa
por el motor IVANNA aunque el módulo Magisk esté activo y el daemon arriba.

## Archivos a corregir (territorio del flanco Daemon — NO los toco)

```
magisk_module/vendor_base/sku_blair_audio_effects.xml     (línea ~54)
magisk_module/vendor_base/sku_holi_audio_effects.xml      (línea ~98)
magisk_module/system/vendor/etc/audio_effects.xml         (línea ~39)
magisk_module/system/etc/audio_effects_ivanna_omega.xml   (línea ~8)
magisk_module/system/etc/audio_effects_ivanna.xml         (línea ~38)
```

En cada uno, reemplazar el atributo del efecto omega_effect:

```diff
- uuid="8d7d5e0a-a6eb-4fde-a0ff-cb1b2dd7275e"
+ uuid="4956414e-4e41-4f4d-4547-415355505245"
```

## Ya corregido por mi flanco

- `vendor/etc/audio_effects.xml` (raíz, único audio_effects FUERA de
  `magisk_module/`) — commit `8015b8cb`, XML validado con parser.
- Comando de verificación tras el fix del Daemon:
  ```bash
  grep -rn '8d7d5e0a' --include='*.xml' magisk_module/   # debe quedar vacío
  grep -rc '4956414e-4e41-4f4d-4547-415355505245' magisk_module/**/*.xml
  ```
