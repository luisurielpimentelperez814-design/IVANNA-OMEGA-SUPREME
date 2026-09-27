/**
 * cppFiles — fuentes C++ reales del DSP para el CodeExporter.
 *
 * Diseño: los archivos se importan DIRECTAMENTE del árbol real
 * (app/src/main/cpp/) con el sufijo `?raw` de Vite. Esto elimina la
 * duplicación snapshot/copia que existía antes (996 líneas de C++ embebido
 * como strings que se desactualizaban solas — declaraban "IvannaFusion 2.0.0"
 * y hasta archivos inexistentes como main.cpp y build_and_release.sh).
 *
 * Ahora el exportador SIEMPRE muestra el código vigente del repo: si el
 * flanco DSP cambia una fuente, el dashboard la refleja en el próximo build
 * sin tocar este archivo.
 */
import type { CppFile } from '../types';

import cmakeLists from '../../app/src/main/cpp/CMakeLists.txt?raw';
import ivannaTinyMLHpp from '../../app/src/main/cpp/IvannaTinyML.hpp?raw';
import ivannaTinyMLCpp from '../../app/src/main/cpp/IvannaTinyML.cpp?raw';
import ivannaFusionCoreHpp from '../../app/src/main/cpp/IvannaFusionCore.hpp?raw';
import ivannaFusionCoreCpp from '../../app/src/main/cpp/IvannaFusionCore.cpp?raw';
import evolutionaryEQHpp from '../../app/src/main/cpp/EvolutionaryEQ.hpp?raw';
import evolutionaryEQCpp from '../../app/src/main/cpp/EvolutionaryEQ.cpp?raw';
import psychoacousticsHpp from '../../app/src/main/cpp/Psychoacoustics.hpp?raw';
import psychoacousticsCpp from '../../app/src/main/cpp/Psychoacoustics.cpp?raw';
import hrtfManagerHpp from '../../app/src/main/cpp/HrtfManager.hpp?raw';
import hrtfManagerCpp from '../../app/src/main/cpp/HrtfManager.cpp?raw';
import ivannaNeuromorphicTinyMLHpp from '../../app/src/main/cpp/IvannaNeuromorphicTinyML.hpp?raw';
import ivannaAudioPipelineHpp from '../../app/src/main/cpp/spatial/IvannaAudioPipeline.hpp?raw';
import perfAuditorHpp from '../../app/src/main/cpp/spatial/PerfAuditor.hpp?raw';
import stereoDecomposerHpp from '../../app/src/main/cpp/spatial/StereoObjectDecomposer.hpp?raw';
import acousticRealityHyperengineHpp from '../../app/src/main/cpp/include/acoustic_reality_hyperengine.hpp?raw';
import wfsRendererHpp from '../../app/src/main/cpp/spatial/WfsRenderer.hpp?raw';
import wfsRendererCpp from '../../app/src/main/cpp/spatial/WfsRenderer.cpp?raw';
import testAcousticRealityCpp from '../../app/src/main/cpp/tests/regression/test_acoustic_reality_hyperengine.cpp?raw';

export const CPP_FILES: CppFile[] = [
  {
    filename: 'acoustic_reality_hyperengine.hpp',
    category: 'header',
    description: 'ACOUSTIC REALITY RECONSTRUCTION HYPERENGINE (Fases 1 a 8): AcousticGenome, MicroDetailMap, AcousticTimeMachine, NeuralInferenceCore, PersonalAuditoryModel, PerceptualOptimizer y AcousticRealityOrchestrator',
    content: acousticRealityHyperengineHpp,
  },
  {
    filename: 'WfsRenderer.hpp',
    category: 'header',
    description: 'Fase 6: 4D Field Synthesis Supreme — Objetos 4D con velocidad continua (vx/vy/vz), profundidad variable, elevación Z e interacción con habitación',
    content: wfsRendererHpp,
  },
  {
    filename: 'WfsRenderer.cpp',
    category: 'source',
    description: 'Fase 6: Implementación 4D Field Synthesis Supreme (ley inversa de distancia, integración cinemática continua y reflexiones de primer orden)',
    content: wfsRendererCpp,
  },
  {
    filename: 'test_acoustic_reality_hyperengine.cpp',
    category: 'source',
    description: 'Suite de Validación Científica (6 Pruebas): Concierto vs Reconstrucción, Localización 4D, Profundidad, Realismo de Sala, Carga CPU y Latencia Cero',
    content: testAcousticRealityCpp,
  },
  {
    filename: 'CMakeLists.txt',
    category: 'build',
    description: 'Configuración de build CMake del DSP nativo (ARMv8 NEON, arm64-v8a)',
    content: cmakeLists,
  },
  {
    filename: 'IvannaFusionCore.hpp',
    category: 'header',
    description: 'Núcleo DSP: cadena de procesamiento principal del motor de audio',
    content: ivannaFusionCoreHpp,
  },
  {
    filename: 'IvannaFusionCore.cpp',
    category: 'source',
    description: 'Implementación del núcleo DSP — procesamiento por bloques en el hilo de audio',
    content: ivannaFusionCoreCpp,
  },
  {
    filename: 'IvannaTinyML.hpp',
    category: 'header',
    description: 'Clasificador de escena TinyML de baja latencia (speech/music/transient/ambient)',
    content: ivannaTinyMLHpp,
  },
  {
    filename: 'IvannaTinyML.cpp',
    category: 'source',
    description: 'Implementación del clasificador TinyML con ring buffer SPSC lock-free',
    content: ivannaTinyMLCpp,
  },
  {
    filename: 'EvolutionaryEQ.hpp',
    category: 'header',
    description: 'Ecualizador evolutivo (CMA-ES) — optimización genética de la curva de EQ',
    content: evolutionaryEQHpp,
  },
  {
    filename: 'EvolutionaryEQ.cpp',
    category: 'source',
    description: 'Implementación del motor evolutivo de ecualización',
    content: evolutionaryEQCpp,
  },
  {
    filename: 'Psychoacoustics.hpp',
    category: 'header',
    description: 'Modelo psicoacústico — curvas ISO 226, fatiga auditiva y protección vocal',
    content: psychoacousticsHpp,
  },
  {
    filename: 'Psychoacoustics.cpp',
    category: 'source',
    description: 'Implementación del modelo psicoacústico en el hilo de audio',
    content: psychoacousticsCpp,
  },
  {
    filename: 'HrtfManager.hpp',
    category: 'header',
    description: 'Gestor HRTF — espacialización binaural con convolución por particiones',
    content: hrtfManagerHpp,
  },
  {
    filename: 'HrtfManager.cpp',
    category: 'source',
    description: 'Implementación del convolver HRTF optimizado a NEON',
    content: hrtfManagerCpp,
  },
  {
    filename: 'IvannaNeuromorphicTinyML.hpp',
    category: 'header',
    description: 'Motor TinyML neuromórfico (Eje Anti-Dolby) — Pi-LSTM e inferencia sub-milisegundo sin locks',
    content: ivannaNeuromorphicTinyMLHpp,
  },
  {
    filename: 'IvannaAudioPipeline.hpp',
    category: 'header',
    description: 'Pipeline Espacial 6 Ejes — Orquestación integral de descomposición, HRTF, sala, objetos y adaptación auditiva',
    content: ivannaAudioPipelineHpp,
  },
  {
    filename: 'StereoObjectDecomposer.hpp',
    category: 'header',
    description: 'Eje 1: Descomposición estéreo a 4 objetos acústicos WFS (Center, Left, Right, Ambient)',
    content: stereoDecomposerHpp,
  },
  {
    filename: 'PerfAuditor.hpp',
    category: 'header',
    description: 'Eje 7: Auditoría continua de rendimiento RT-Safety, latencia cero añadida y monitor de L1 cache',
    content: perfAuditorHpp,
  },
];

/**
 * Genera el bloque bash 1-click para Termux con TODAS las fuentes reales
 * en formato `cat << 'EOF'`. El usuario lo pega en Termux y obtiene el
 * árbol de fuentes exacto del repo para compilar localmente.
 */
export function generateFullTermuxScript(): string {
  let script = `#!/usr/bin/env bash
# ==============================================================================
# IVANNA OMEGA SUPREME v2.3.6 — Fuentes C++ del DSP nativo
# Bloque de extracción rápida para Termux / shell Linux
# ==============================================================================

echo "[IVANNA] Creando workspace de fuentes DSP (app/src/main/cpp)..."
mkdir -p ivanna_dsp_src && cd ivanna_dsp_src

`;

  for (const file of CPP_FILES) {
    script += `cat << 'EOF' > ${file.filename}\n${file.content}\nEOF\n\n`;
  }

  script += `echo "[IVANNA] ${CPP_FILES.length} archivos extraidos. Compila con: cmake -B build && cmake --build build"
`;

  return script;
}
