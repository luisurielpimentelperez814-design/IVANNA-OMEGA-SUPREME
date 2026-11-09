# Ficha 18 — Compresión multibanda

**Dominio:** Procesamiento de señal avanzado · **Estado:** en producción (compresor banda ancha + EQ)
**Fuentes:** `VEREDICTO.md` §2.1 paso 9, §3#5; `FUSION.md` §1.6.

## Concepto

Un compresor reduce el rango dinámico: atenúa lo fuerte y deja lo suave.
La compresión **multibanda** lo hace por separado en cada banda de frecuencia:
los graves pueden comprimirse sin tocar los agudos. Es lo que permite que una
mezcla suene "densa" sin aplastar los transitorios. El riesgo: si las bandas
se cruzan mal, el sonido "respira" (pumping) o cambia de timbre con el nivel.

## Definición y valores (compresor de IVANNA)

- **Detector en dos etapas:** la 2ª filtra el rizado a 2f de |x| (el rizado
  del detector es lo que distorsiona en graves).
- **Parámetros por defecto:** threshold −12 dB, ratio 4:1; runtime overlay
  del motor adaptativo (threshold −20 dB base + delta V2, ratio hasta 1+7·amount).
- **SafetyLimiter:** threshold −1 dBFS, ceiling −0.1 dBFS, ataque 1.5 ms,
  release 50 ms. Techo inviolable con senos a +5 dBFS.
- **Asentamiento de ganancia** independiente del tamaño de bloque (64/480/1024)
  en ~20 ms.

## Mediciones (THD+N del compresor solo, host)

| Frecuencia | Antes de la 2ª etapa | Después (suelos exigidos) |
|---|---|---|
| 150 Hz | −61 dB | **−73 dB** |
| 500 Hz | — | **−93 dB** |
| 1 kHz | −76 dB | **−105 dB** |
| 5 kHz | −89 dB | **−129 dB** |

- `test_limiter_route_params`: transparente bajo umbral, ≤1 dB de reducción
  a 0 dBFS.
- **La compresión multibanda como tal no existe en IVANNA:** hay un compresor
  de banda ancha + EQ de 8 bandas + EvolutionaryEQ. La multibanda real
  (crossover + compresor por banda) es trabajo pendiente.

## Dónde se usa en IVANNA

| Componente | Archivo | Detalle |
|---|---|---|
| `Compressor` | `app/src/main/cpp/dsp/Compressor.cpp` | Banda ancha, 2 etapas, knee suave. |
| `SafetyLimiter` | Cadena Ruta A/B | Pico verdadero, último guardián. |
| `StabilityGuard` | Cadena | Techo 0.92 / 1.15–1.20× por eje supremo. |

## Preguntas abiertas (para la evolución)

1. ¿Compresor multibanda por bandas ERB (ficha 08) en vez de banda ancha?
   ¿Reduce la THD en graves sin tocar los agudos?
2. ¿El compresor debe ser parte de Γ_b de HOLOS (ficha 03) en vez de un
   módulo aparte? (HOLOS ya lo dice: la ganancia es UNA por banda.)
3. ¿Lookahead con retardo compensado (no gratis: +latencia) vale la pena
   para transitorios?
4. ¿Compresión paralela (New York) como modo: seco + comprimido mezclados?
