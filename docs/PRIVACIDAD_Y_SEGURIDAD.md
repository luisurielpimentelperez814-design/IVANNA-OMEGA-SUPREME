# 🔐 IVANNA OMEGA SUPREME — Privacidad y Seguridad

Documento de producto (auditoría 2026-09-08). Explica qué permisos usa la
app **y por qué**, y qué información sale del dispositivo. Objetivo:
dejar el producto listo para una declaración de privacidad en Play/F-Droid
y para que un usuario técnico pueda auditar el comportamiento.

## Permisos declarados (AndroidManifest.xml) y su propósito
| Permiso | Propósito en IVANNA | Riesgo | Notas |
|---|---|---|---|
| `RECORD_AUDIO` | Captura de audio para el asistente conversacional (voz) y análisis | Alto | Solo activo al usar la voz; pedir en runtime |
| `PACKAGE_USAGE_STATS` | Monitorear qué app reproduce audio (panel de estado de sesiones) | Alto | Requiere pantalla de activación especial; documentar |
| `MEDIA_CONTENT_CONTROL` | Saber qué sesión de audio está activa | Medio | Útil para el control maestro |
| `MODIFY_AUDIO_SETTINGS` | Ajustar volumen/efectos del sistema | Medio | Inofensivo |
| `FOREGROUND_SERVICE_*` (MEDIA_PLAYBACK/MEDIA_PROJECTION) | Servicios en primer plano para captura y procesamiento | Medio | Obligatorio desde Android 14 |
| `RECEIVE_BOOT_COMPLETED` / `QUICKBOOT` | Reanudar el servicio tras reinicio | Medio | |
| `REQUEST_IGNORE_BATTERY_OPTIMIZATIONS` | Evitar que el sistema mate el hilo de audio | Medio | Justificar en Play |
| `INTERNET` / red / wifi | Asistente Gemini y dashboard web | Medio | Ver "Qué sale del dispositivo" |
| `WAKE_LOCK` / `VIBRATE` | Mantener CPU para el pipeline de audio | Bajo | |

## Qué sale del dispositivo
1. **Asistente Gemini (opcional):** cuando el usuario chatea por voz/texto, el
   texto y el contexto de la conversación viajan a la API de Gemini
   (`ai.gemini`). Política: no se envían muestras de audio ni metadatos del
   dispositivo; solo texto de usuario + prompt construido localmente.
2. **Dashboard web (self-hosted):** si se despliega el panel de control, el
   servidor Express solo habla con el dispositivo del propietario. Sin
   despliegue público no hay tráfico.
3. **Nada más por defecto:** la cadena DSP, HRTF y la clasificación TinyML
   son 100% en dispositivo (C++ nativo); IVANNA no sube audio, ni perfiles
   de uso, ni telemetría de la app.

## Estado actualizado de permisos (auditoría 2026-10-18)

La captura de salida en Ruta A se autoriza mediante MediaProjection y consentimiento del sistema; la app no necesita declarar el permiso privilegiado `CAPTURE_AUDIO_OUTPUT`. Se retiraron del manifest `CAPTURE_AUDIO_OUTPUT`, `READ_LOGS` y `BIND_AUDIO_EFFECT_SERVICE`: no había llamadas de aplicación que justificaran esas declaraciones y no existía un servicio de efecto Android declarado que las usara. La comunicación con el daemon valida `SO_PEERCRED` en ambos sockets UNIX antes de ejecutar comandos o entregar el descriptor de memoria compartida; se permite la app instalada, root y shell administrativo. El listener TCP opcional se deshabilitó por no tener autenticación. Esta defensa queda pendiente de CI y validación en dispositivo.

Las claves de Gemini se guardan mediante `SecureConfigurationManager`; la copia heredada en preferencias sin cifrar se elimina durante la migración. Una clave incluida en un APK no se considera secreta.

## Recomendaciones antes de publicar en una tienda
1. Solicitar permisos sensibles en runtime con explicación (no solo en
   manifest).
3. Redactar la "Declaración de datos" de Play: sin recolección de audio,
   uso de Gemini opcional (datos de texto), diagnóstico local.
4. Documentar el acceso a `PACKAGE_USAGE_STATS` con pantalla de activación.
5. Revisar política de datos de terceros (Gemini/Firebase) en el data safety
   form.

## Seguridad de la infraestructura (auditoría)
- El dashboard web se ha endurecido en paralelo por su flanco (rate-limit y
  validación de entrada en `/api/chat`); auth por bearer token opcional ya
  implementada (commit `f52a7674`) para cuando se exponga fuera de localhost.
- El daemon root ejecuta con SCHED_FIFO y controles SELinux; el repo no
  versiona binarios ni secretos (verificado con grep en auditoría).

## Cadena de suministro
- CI con SBOM (syft, SPDX+CycloneDX), escaneo Trivy y firmas cosign
  (workflow supply-chain.yml) — verificado presente en el repo.
