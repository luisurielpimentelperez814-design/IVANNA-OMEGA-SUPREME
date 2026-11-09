# Ficha 02 — Volterra (serie con memoria del transductor)

**Dominio:** B (matemáticas/DSP) · **Estado:** medido (kernel PZ1D), en producción
**Fuentes:** `FUSION.md` §1.6, §2.2; `FRONTERA.md` §2.2; `docs/GENEZY_MHC_PZ1D_KERNEL.md`.

## Concepto

Un altavoz o auricular real no es lineal: su salida depende del nivel y de
la historia reciente (la suspensión se endurece al final de carrera, el
factor de fuerza `Bl(x)` y la inductancia `Le(x)` dependen del desplazamiento).
La serie de Volterra modela esa no-linealidad CON memoria: la salida es la
suma de un término lineal (h1), uno cuadrático con memoria (h2, matriz
triangular) y uno cúbico (h3). Un waveshaper sin memoria (Chebyshev, tanh)
no puede producir lo que Volterra sí: distorsión que depende de la historia,
IMD entre bandas, y dependencia del nivel no polinómica.

## Definición y fórmulas

```
y[n] = Σ_{k=0..K-1} h1[k]·x[n−k]
     + Σ_{k=0..K-1} Σ_{l=k..K-1} (k==l ? 1 : 2)·h2[k,l]·x[n−k]·x[n−l]
```

- `K = 64` taps, 2080 coeficientes h2 (triangular), 2 canales.
- **Kernel disperso:** los kernels medidos reales son dispersos (~10% de
  coeficientes no nulos). El PZ1D tiene solo **16 coeficientes h2 no nulos**
  (diagonal): H2 pasa de 2080 a 16 MAC/muestra (130× menos trabajo).
- **Puerta IVK1:** formato de paquete con CRC-32, límites de seguridad,
  fail-closed (sin kernel medido = identidad bit-exacta).

## Valores medidos (kernel MOTO_G85_PZ1D, el default)

| Parámetro | Valor |
|---|---|
| h1 (64 taps) | L1 = 5.00097, DC = 3.76669, max\|h1\| = 0.7142 (tap 15) |
| h2 (solo diagonal, 16 valores) | sum\|h2\| = 0.144146, max 0.0258 |
| Normalización | ÷L1 = 5.00097 (DC → 0.753, −2.46 dB) |
| Archivo | `moto_g85_pz1d.ivk1` (8616 B, SHA-256 `0fc035c1…`) |
| Kernel alternativo | Genezi MHC-PZ1D (`genezy_mhc_pz1d.ivk1`, 8616 B, SHA-256 `a6883e5f…`), h1 L1 = 4.391953 |

- Optimización del motor: H2 dispersa + línea de retardo circular O(1) + H1
  vectorizado NEON. Throughput host: 1.5× RT → **~68× RT**.
- Selector en caliente: `VolterraH2Symmetric::replaceKernelPackage()` atómico
  (la puerta valida el paquete nuevo PRIMERO; si lo rechaza, el motor no se
  toca). Tests `KernelSwitch` 8/8.

## Dónde se usa en IVANNA

| Componente | Archivo | Detalle |
|---|---|---|
| `VolterraH2Symmetric` | `app/src/main/cpp/neuromorphic/volterra_h2_symmetric.{hpp,cpp}` | Motor H1+H2, K=64, optimizado NEON + disperso. |
| Puerta IVK1 | `app/src/main/cpp/neuromorphic/volterra_kernel_package.hpp` | Carga con CRC-32 y límites; fail-closed. |
| Cableado Ruta A/B | JNI + Kotlin (UI toggle VOLTERRA H2); Ruta B busca el .ivk1 en 3 rutas. |
| Selector | `docs/VOLTERRA_KERNEL_SWITCH.md` | PZ1D REFINADO / GENEZI, persiste tras reinicio. |

## Mediciones que lo respaldan

- Puerta ACEPTADA en Python (l1=1.0) y C++ Ok, para ambos kernels.
- Tests: 7/7 GeneziKernel, 10/10 PZ1D; suite 837/837 (v2.5.369).
- **Pendiente:** re-medición en vivo, puerta ABX en G85, CPU/XRuns con
  Volterra activo. El kernel actual es tabulado del PDF (Farina ESS), NO
  re-medición en vivo del hardware.

## Preguntas abiertas (para la evolución)

1. ¿El kernel medido predice la THD/IMD real mejor que un Chebyshev
   genérico (<1 dB por armónico en señal no usada para identificar)?
   (FRONTERA §2.2 — la dirección con más probabilidad de ser física nueva.)
2. ¿Kernel por banda ERB (24 bandas) en vez de banda ancha? Solo las bandas
   <1 kHz llevarían memoria (~1000 MAC/muestra).
3. ¿Compensación (invertir el kernel) o explotación (usar la no-linealidad
   como color)? ¿Cuál suena mejor en ABX con niveles igualados?
4. ¿Kernel distinto por auricular conectado (detección + carga automática)?

## Hueco 7 cerrado — Kernel Volterra por banda medido en vivo (2026-11-09)

**Estado: LISTO PARA MEDIR.** Protocolo ESS por banda ERB (barrido senoidal
exponencial 20 Hz–20 kHz, 10 s, −14 dBFS; deconvolución por banda → H1; 2º
armónico → H2 diagonal) + empaquetado IVK1 (`tools/volterra_h2/kernel_loader.py`)
+ carga en caliente (`replaceKernelPackage()`). Detalle completo en
`docs/KNOWLEDGE_CORE/GAPS_FILLED.md` §7. Límite honesto: el kernel medido en vivo
es la cadena completa bocina+sala+mic, no la cadena del Genezi.
