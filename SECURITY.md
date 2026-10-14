# Política de seguridad

IVANNA OMEGA SUPREME incluye un **daemon con privilegios de root** (módulo Magisk), un efecto de audio cargado en
`audioserver` y un socket de control local. Por eso se toma en serio cualquier fallo que permita escalar
privilegios, ejecutar código o alterar el audio de otras apps sin consentimiento.

## Versiones con soporte

Solo se corrige la **última versión publicada** (`version.properties` / Releases). Actualiza antes de reportar.

## Cómo reportar una vulnerabilidad

**No abras un issue público.** Usa *Security → Report a vulnerability* (reporte privado de GitHub) en este
repositorio. Incluye: versión, dispositivo/Android/Magisk, pasos de reproducción, impacto esperado y, si puedes,
una propuesta de corrección. Se acusará recibo lo antes posible y se coordinará la divulgación contigo.

## Alcance de interés

- Escalada de privilegios mediante el socket `omega_command_socket`, el SHM del bus de control o el módulo.
- Lectura/escritura fuera de límites en el procesado nativo (JNI, DSP, parseo de datasets `IHR1`/`IVW1`/SOFA).
- Carga de datasets o modelos manipulados que puedan ejecutar código o bloquear el audio del sistema.
- Fugas de datos de audio o de telemetría fuera del dispositivo. Lo que sale por diseño está documentado en `docs/PRIVACIDAD_Y_SEGURIDAD.md` (texto del asistente Gemini, opcional; sincronización Firebase, opt-in). Cualquier otro tráfico es un hallazgo.

Fuera de alcance: requerir root o desbloqueo de bootloader por diseño, ataques que exigen acceso físico ya
desbloqueado, y reportes sin impacto de seguridad demostrable (usa un issue normal).

## Buenas prácticas ya aplicadas

Puertas estáticas en CI (RT-safety, cableado JNI, *supply chain*), pruebas bajo ASan/UBSan y escaneo de secretos.
Más detalle en `docs/PRIVACIDAD_Y_SEGURIDAD.md`.
