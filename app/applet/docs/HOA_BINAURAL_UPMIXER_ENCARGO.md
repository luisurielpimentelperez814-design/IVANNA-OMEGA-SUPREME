# ENCARGO — Completar HOA → Binaural → Upmixing (versión corregida contra el código real)

> Este documento reemplaza al prompt original "HOA + Binaural + Upmixing"
> del propietario (2026-09-15). Ese prompt daba por confirmadas cosas que
> **no existen en el repo** (verificado exhaustivamente: `main` + las ~40
> ramas remotas, y `git log --all --grep` sobre todo el historial). Este
> documento corrige esas premisas para que quien lo ejecute construya sobre
> lo que de verdad hay, no sobre lo que el prompt asumía.
>
> Regla inviolable (heredada del encargo original, sigue aplicando):
> **construyes SOLO sobre código que existe realmente. Nada decorativo.
> Todo conectado al flujo vivo. Verificar antes de confiar, nunca fabricar
> métricas ni funcionalidad.**

---

## ESTADO DE PARTIDA — VERIFICADO, NO ASUMIDO

✅ Ya existe y está en `main` (commit `06defa02`):
- `app/src/main/cpp/spatial/HoaGainMatrix.hpp` — codificación Ambisonics
  real, SN3D/ACN, orden 0–2, plano horizontal (elevación=0 exacta, no
  aproximada). 6 tests reales en `tests/test_hoa_gain_matrix.cpp`.

❌ NO existe en ninguna rama (verificado, no repetir la búsqueda):
- `HoaBinauralDecoder` — no hay decodificador de HOA a altavoces virtuales.
- `IntelligentUpmixer` — no hay upmixer estéreo→HOA.
- Cualquier rama `feature/hoa-*` con este trabajo — no existe.
- `setActiveHrtfProfile()` sobre este flujo — no existe.

⚠️ Limitaciones reales de la infraestructura existente (afectan el diseño,
no son opcionales):
- `HRTFConvolver::set_position(float azimuthDeg, float aggressiveness)`
  (`spatial/hrtf_convolver.hpp`) es **2D, plano horizontal, sin parámetro
  de elevación**. No hay altura real disponible hoy. Cualquier "platillo
  elevado" tendrá que simularse con otro recurso (p. ej. filtrado
  espectral tipo pinna, no con azimuth/elevación real) o quedar como
  trabajo futuro explícito — nunca fingir elevación con un valor inventado.
- El clasificador de audio real (`IvannaAudioClassifier`,
  `cpp/IvannaAudioClassifier.hpp`) da **escena completa** (`AudioContextClass`
  + confianza + energía), **no separación de fuentes** (no distingue
  centro/lados/bajos/transientes por separado). Cualquier upmixer que
  dependa de esa separación tiene que construirla — no existe hoy.

---

## FASE 1 — Upmixing estéreo → campo HOA (rediseñada, sin CRNN inexistente)

El prompt original pedía reusar "el clasificador CRNN que ya distingue
centro/lados/bajos/transientes cada 50ms". **Ese clasificador no existe.**
Alternativa real, construible con lo que sí hay (procesamiento de señal
clásico, sin modelo nuevo):

- **Descomposición mid-side real:** `mid = (L+R)/2`, `side = (L-R)/2` —
  esto YA separa centro (mid, alta correlación) de laterales (side) sin
  necesitar ningún clasificador. Es la técnica estándar de upmixing
  pasivo, verificable matemáticamente.
- **Separación de graves por filtrado real** (no detección): un
  cruce (crossover) de fase lineal o mínima a ~150–200Hz sobre la señal
  `mid` ya aísla el contenido grave de forma determinista.
- **Transientes:** si se quiere tratarlos distinto (más "arriba" con el
  truco espectral, no elevación real), un detector de transientes simple
  y real (derivada de envolvente / onset por energía) es implementable
  desde cero — dejarlo documentado como su propio sub-módulo, con sus
  propios tests, NO mezclado silenciosamente dentro del upmixer.
- Todo lo demás del diseño original (parámetro `inmersividad` 0.0–1.0,
  modo transparente con bypass a estéreo puro si no hay confianza,
  sin `malloc` en el hilo de audio, suavizado por bloque) sigue siendo
  válido y deseable — construirlo sobre las fuentes reales de arriba,
  no sobre el CRNN inexistente.

Módulo: `spatial/IntelligentUpmixer.hpp/.cpp`. Debe producir un `HoaVector`
(el tipo ya real de `HoaGainMatrix.hpp`) por bloque, usando
`HoaGainMatrix::encode()` + `accumulate()` ya existentes — no reinventar
la codificación.

Tests reales a escribir (no opcionales):
- Suma de dos canales estéreo idénticos (mono real) → toda la energía cae
  en W y en la componente frontal (X), lateral (Y) ≈ 0.
- Estéreo completamente descorrelacionado (ruido L/R independiente) →
  energía repartida de forma verificable en el campo, no un valor mágico.
- Modo transparente: con la señal de entrada = 0, salida = campo HOA nulo
  (identidad), sin NaN/Inf.
- Bypass explícito (`setUpmixingEnabled(false)`): salida idéntica bit-a-bit
  a pasar el estéreo sin tocar.

---

## FASE 2 — HoaBinauralDecoder (nuevo, real)

Diseño concreto, usando SOLO `HRTFConvolver` (2D) ya existente:

- N altavoces virtuales distribuidos en un **anillo horizontal** (p. ej.
  8 posiciones a 45°) — sin pretender elevación real, ver limitación de
  arriba.
- Un `HRTFConvolver` real por altavoz virtual, cada uno con su propio
  `set_position(azimuth_k, aggressiveness)`.
- Matriz de decodificación de los 9 canales HOA a la señal de cada
  altavoz: **derivar y verificar la normalización con cuidado** (no
  copiar una fórmula sin comprobarla). Antes de escribir código,
  verificar con un test de round-trip: codificar una fuente puntual con
  `HoaGainMatrix::encode(az)`, decodificar a los N altavoces, y confirmar
  que el altavoz más cercano a `az` recibe la ganancia máxima y que la
  energía total decodificada es consistente entre distintos azimuths
  (sin picos ni huecos anómalos al barrer 0–360°).
- Sumar la salida estéreo de los N convolvers → salida binaural final.
- Costo real: N convolvers en paralelo es N× el costo de uno — medir el
  impacto real en el hilo de audio (no asumir que es gratis) antes de
  fijar N definitivo.

Tests reales:
- Round-trip azimuth descrito arriba, barriendo 0–360° en pasos finos.
- Fuente en W puro (mono, sin dirección) → todos los altavoces con
  ganancia igual (dentro de tolerancia).
- Sin entrada → salida silenciosa, sin NaN/Inf.

---

## FASE 3 — Cableado real en `ivanna_fusion_engine.cpp/.hpp`

Solo después de que Fase 1 y Fase 2 tengan sus tests en verde:

```
ENTRADA ESTÉREO → PDEngine (ya existe) → IntelligentUpmixer (Fase 1)
→ HoaBinauralDecoder (Fase 2) → SafetyLimiter (ya existe) → SALIDA
```

Interfaz pública mínima (ajustar a lo que el motor real necesite, no
copiar literal si no encaja):
```cpp
void setUpmixingEnabled(bool enable);
bool isUpmixingEnabled() const;
void setImmersivity(float value);   // 0.0 estéreo puro .. 1.0 máximo campo
float getImmersivity() const;
```

Comportamiento honesto obligatorio:
- Si el upmixer está en modo transparente (baja confianza / silencio) →
  bypass real a estéreo, no un campo HOA "casi vacío" que suene distinto.
- Sin perfil HRTF cargado → HRTF genérico ya existente como respaldo (no
  inventar uno nuevo).
- Degradación térmica: reusar la señal térmica real que ya existe en el
  motor (ver `AgentState.health`), no inventar un sensor nuevo.

---

## FASE 4 — Límites honestos (`docs/IMPLEMENTATION_NOTES.md`)

Actualizar esa nota (no crear una paralela) con, como mínimo:
- Qué es real: upmixing mid-side + banda de graves, decodificación a N
  altavoces virtuales horizontales, todo binaural vía `HRTFConvolver` real.
- Qué NO es: no hay elevación real (ver limitación de `HRTFConvolver`),
  no hay separación de fuentes por IA (es procesamiento de señal clásico,
  dicho así, no maquillado de "IA").
- Igual que el prompt original pedía: WFS no existe, orden >2 pendiente
  de verificación, no reconstruye la grabación original.

---

## FASE 5 — Pruebas (adicional a las ya listadas en cada fase)

- Validación de que el pipeline completo (Fase 3) no introduce clipping
  nuevo: correr con ASan+UBSan, señal de prueba a 0dBFS de pico, verificar
  `|salida| ≤ 1` tras el `SafetyLimiter`.
- Medición real de latencia añadida por el upmixer+decoder (no una cifra
  supuesta) — reportarla en el commit, con el método de medición.

---

## ENTREGA

- Cada fase en su propio commit (o varios si la fase es grande),
  compilando y con tests en verde ANTES del siguiente commit — no
  acumular cambios sin validar.
- `AGENT_CLAIMS.md`: actualizar el mismo bloque de coordinación existente
  (el de "Encargo directo adicional del propietario / HOA→Binaural") con
  cada avance real, igual que se ha venido haciendo — no crear una entrada
  nueva suelta.
- **Al terminar TODA la labor** (Fases 1–5 con tests en verde y cableado
  real confirmado en CI): actualizar `README.md` del proyecto añadiendo la
  nueva cualidad — audio espacial HOA/Ambisonics personalizado, upmixing
  automático de cualquier estéreo, integrado con el HRTF personalizado ya
  existente — con el mismo nivel de honestidad que el resto de este
  documento: decir lo que hace y lo que no (sin elevación real, sin
  separación de fuentes por IA), no una frase de marketing.
- Si en cualquier fase una premisa de este documento resulta también
  incorrecta al verificarla contra el código, documentarlo en
  `AGENT_CLAIMS.md` igual que se hizo con las dos premisas falsas del
  prompt original, y ajustar el plan — no forzar una implementación sobre
  una base que no está.
