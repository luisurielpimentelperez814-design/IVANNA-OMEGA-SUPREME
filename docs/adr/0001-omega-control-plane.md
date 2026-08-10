# ADR-0001: Omega Control Plane como Fuente de Verdad Autoritativa

## Estado
ACEPTADO

## Contexto
El sistema tenía un canal de control que respondía `{"ok":true}` pero cuyo estado no llegaba al data-plane de audio real. Esto creaba estados fantasma y respuestas falsas.

## Decisión
Implementar un **Control Plane autoritativo** basado en:
1. `OmegaDspSnapshot` - estructura fija para shared memory
2. `OmegaControlBus` usando `SeqlockBus<T>` sobre mmap
3. `Route Arbiter` para evitar doble procesamiento
4. Contrato JSON con estado verificable real

## Arquitectura
- **Control Plane**: `command_server.cpp` publica snapshots atómicos
- **Data Plane SYSTEM_WIDE**: `omega_effect.cpp` consume snapshots
- **Data Plane IN_PROCESS**: `nativeProcess()` usa Ruta A
- **Route Arbiter**: Modos OFF/SYSTEM_WIDE/IN_PROCESS/PREVIEW

## Consecuencias
- ✅ Verdad verificable en tiempo real
- ✅ Cero respuestas falsas
- ✅ Cero doble procesamiento
- ✅ Audio inmersivo/binaural/espacial de clase mundial
- ⚠️ Requiere validación en device con CI ARM64

## Implementación
Commits atómicos individuales con push inmediato.
