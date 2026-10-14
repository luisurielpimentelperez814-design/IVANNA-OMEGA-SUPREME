## ¿Qué cambia y por qué?

<!-- Una o dos frases. Enlaza el issue si existe: Fixes #123 -->

## ¿Cómo se verificó?

- [ ] Prueba nueva o ajustada en `app/src/main/cpp/tests/` (o explico por qué no aplica)
- [ ] `bash scripts/run_ctest.sh` en verde
- [ ] Puertas estáticas en verde: `for g in scripts/check_*.py; do python3 "$g"; done`
- [ ] Si añadí pruebas, sincronicé el conteo de CTest en `README.md`, `LÉAME.md` y `telemetry/ctest_summary.json`
- [ ] Distingo lo **medido en host** de lo **escuchado en dispositivo** (indica cuál es cada cosa)

## Notas de tiempo real y compatibilidad

- [ ] No añade `malloc`, locks ni I/O al hilo de audio
- [ ] No borra código: lo mejora, lo cablea o lo documenta
- [ ] No incluye material de terceros sin declararlo (ver `THIRD_PARTY_NOTICES.md`)
