# Cómo contribuir a IVANNA OMEGA SUPREME

¡Gracias por querer ayudar! Este proyecto es un motor de audio para Android (C++20 nativo + Kotlin/Compose) con
una Ruta A (sin root, dentro de la app) y una Ruta B (con root, módulo Magisk + daemon). Lo que más falta es
**validación en dispositivos reales**, así que una prueba bien documentada vale tanto como un parche.

## Formas de ayudar (de menor a mayor esfuerzo)

1. **Probar en tu equipo** y abrir un *Reporte de dispositivo* (plantilla en Issues): modelo, Android, versión de
   Magisk, ruta usada (A/B) y qué escuchaste. Sin ese dato no se puede afinar nada.
2. **Reportar errores** con la plantilla de *Error*: pasos, versión de la app (`version.properties`) y logs.
3. **Mejorar documentación y traducciones** (la mayoría está en español; una guía de inicio en inglés es muy bienvenida).
4. **Escribir pruebas** (`app/src/main/cpp/tests/`): cualquier comportamiento DSP sin prueba es una buena primera tarea.
5. **Corregir código**: busca los issues con `good first issue` o `help wanted`.

## Preparar el entorno (host, sin Android)

```bash
git clone https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME.git
cd IVANNA-OMEGA-SUPREME
pip install cmake                                  # si no lo tienes
bash scripts/run_ctest.sh                          # compila y corre toda la suite en host
for g in scripts/check_*.py; do python3 "$g"; done # las puertas estáticas
```

Las pruebas de host cubren el DSP, el plano de control (daemon ↔ snapshot) y las puertas de calidad. El APK y el
módulo Magisk se compilan en CI; Kotlin no se puede compilar sin el SDK de Android.

## Reglas del proyecto (las hace cumplir el CI)

- **Nada de humo técnico.** Toda mejora debe tener implementación real, **medición y prueba**. No se aceptan
  cifras inventadas ni afirmaciones de "mejor que X" sin una medición reproducible (`scripts/check_docs_claims.py`).
- **No se borra: se mejora o se documenta.** Código aparentemente muerto se revive y se cablea (UI → JNI → nativo
  → DSP) o se explica por qué existe. Las puertas `check_header_wiring`, `check_jni_wiring` y
  `check_kotlin_wrapper_wiring` detectan lo desconectado.
- **Tiempo real primero.** En el hilo de audio: sin `malloc`, sin locks, sin I/O (`scripts/check_rt_safety.py`).
- **Un cambio lógico por commit**, con mensaje que explique el *porqué* y cómo se verificó. Si añades pruebas,
  sincroniza el conteo de CTest que citan `README.md`/`LÉAME.md` y `telemetry/ctest_summary.json`.
- **Honestidad sobre lo verificado.** Distingue "medido en host con señales sintéticas" de "escuchado en
  dispositivo". Ambas son útiles; no las mezcles.

## Pull requests

1. Abre primero un issue o una *Discussion* si el cambio es grande.
2. Crea una rama, haz commits pequeños y abre el PR con la plantilla.
3. El CI debe quedar en verde (tests host, ASan/UBSan, puertas estáticas, APK).

## Licencia de las contribuciones

El repositorio aún no declara una licencia explícita (decisión pendiente del titular). Al enviar un PR confirmas
que eres autor/a de lo que aportas y que no incluye código ni datos con licencia incompatible; detalla en el PR
cualquier material de terceros (ver `THIRD_PARTY_NOTICES.md`).

## Conducta

Participar implica aceptar el [Código de Conducta](CODE_OF_CONDUCT.md). Vulnerabilidades: ver [SECURITY.md](SECURITY.md).
