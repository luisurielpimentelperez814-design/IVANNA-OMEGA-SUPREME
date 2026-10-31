# IVANNA Genezi Spatial — especificación canónica y validación

Estado: **beta reversible de expansión estéreo AUX**. El modelo matemático es normativo; la equivalencia exacta del procesamiento nativo y la calibración acústica del Sony siguen pendientes de evidencia.

## 1. Enunciado canónico: modelo ideal

Sea $k \in \mathbb{R}$, con $k=1.08$, y sean $L[n],R[n]$ muestras reales en aritmética ideal. La transformación lineal instantánea de referencia es

\[
\begin{bmatrix}L_{out}[n]\\R_{out}[n]\end{bmatrix}
=\frac12
\begin{bmatrix}1+k&1-k\\1-k&1+k\end{bmatrix}
\begin{bmatrix}L[n]\\R[n]\end{bmatrix}.
\]

Para $k=1.08$:

\[
L_{out}=1.04L-0.04R,\qquad
R_{out}=-0.04L+1.04R.
\]

Equivalente en la base Mid/Side:

\[
M=\frac{L+R}{2},\quad S=\frac{L-R}{2},\qquad
M_{out}=M,\quad S_{out}=kS.
\]

Reconstrucción: $L_{out}=M_{out}+S_{out}$ y $R_{out}=M_{out}-S_{out}$.

Este modelo es una escala instantánea del modo Side. No es un filtro, no tiene memoria, no es infrasónico, no es Haas, no es decorrelación por retardo y no constituye por sí solo HRTF, ambisonics ni sonido 3D.

## 2. Propiedades exactas del modelo

- **Contenido común:** $M_{out}=M$.
- **Side en amplitud:** $S_{out}=1.08S$.
- **Fold-down de esta etapa:** $L_{out}+R_{out}=L+R$.
- **Energía Side:** $\lVert S_{out}\rVert^2=1.1664\lVert S\rVert^2$, si se mantiene la misma señal de entrada.
- **Determinante:** $\det(W)=k=1.08$; la transformación no es ortogonal y no conserva la energía total en general.
- **Autovalores:** $\lambda_M=1$ y $\lambda_S=k$, con autovectores ortogonales $(1,1)$ y $(1,-1)$.
- **Respuesta del modelo ideal:** matriz constante $W$ para toda frecuencia; no introduce por sí misma rotación de fase dependiente de frecuencia.
- **Referencia de diseño:** $20\log_{10}(1.08)\approx0.668\,\mathrm{dB}$ es el aumento de amplitud del modo Side expresado en dB, no una compensación aplicada automáticamente ni una predicción de LUFS.

Estas propiedades son del modelo ideal y no describen automáticamente toda la cadena de audio posterior.

## 3. Correlación y límites perceptuales

El coeficiente de correlación intercanal no es una función monótona universal de $k$. La modificación de Side puede reducir la correlación para ciertas señales, pero no lo hace en mono puro ($S=0$) y el coeficiente no está definido si uno de los canales tiene energía nula. Debe medirse sobre audio válido.

No se declara ningún umbral universal de $k$ para peine, colapso de imagen o fatiga. La percepción depende del programa, transductores, sala y procesamiento de la cadena. $k=1.08$ es una hipótesis inicial moderada y reversible, no un óptimo acústico demostrado.

## 4. Perfil AUX solicitado

El perfil Kotlin de `OutputRoute.WIRED_AUX` declara:

```kotlin
bassBoostDb = 0f
dialogBoostDb = 0f
widenerMult = AtomicSpatialPolicy.geneziSpatialMultiplier() // 1.04f
```

Los ceros declaran que el perfil no solicita boost de graves ni de diálogo. No prueban que toda la cadena mantenga inalterado el bajo o el diálogo.

El propósito es entregar una señal estéreo a la entrada AUX del Sony Genezi MHC-PZ1D y permitir que el equipo aplique su propio procesamiento Surround. IVANNA no genera cinco canales discretos por AUX, no controla los botones Surround/subwoofer del Sony y no presume conocer su transferencia electroacústica.

## 4.1 Compromiso minimax Ω-ATOMIC

La política de ruta combina dos objetivos de control, sin tratarlos como mediciones de sala:

- Foco: multiplicador objetivo $w_F=1.00$ (ancla central/unity).
- Inmersión: multiplicador de referencia $w_I=1.08$ (propuesta AUX anterior).

Se elige el multiplicador que minimiza el peor error cuadrático frente a ambos objetivos:

\[
w^*=\\arg\\min_{w\\in[1.00,1.08]}\\max\\{(w-w_F)^2,(w-w_I)^2\\}.
\]

Para dos objetivos escalares, el minimizador exacto es su punto medio: $w^*=1.04$. La implementación está en `AtomicSpatialPolicy.kt`; el resultado se proyecta al intervalo permitido y los datos no finitos/intervalos inválidos fallan de forma conservadora a unity. Las pruebas JVM verifican el minimax, el peor error, la proyección y los casos inválidos.

Este es un compromiso robusto entre dos consignas de control, no una optimización sobre transferencias de sala desconocidas. No aprende la respuesta del Genezi, no reconstruye stems y no promete localización física fuera del punto de escucha.

## 5. Diferencia confirmada entre el modelo y el DSP nativo actual

**No afirmar equivalencia exacta entre el modelo ideal y el procesamiento nativo actual.** La inspección de `app/src/main/cpp/dsp/StereoWidener.cpp` confirma que el DSP real:
- suaviza `widthNow_` hacia `width_` muestra a muestra (anti-zipper);
- separa Side en componentes pasa-bajos/pasa-altos mediante `sideLpf_`;
- aplica `bassFactor` distinto al Side de baja frecuencia cuando el ancho es mayor que 1;
- aplica bloqueadores de DC por canal;
- conserva estados de filtros durante el camino transparente de unidad.

Además, `audio_control_plane.cpp` calcula `combined_width` a partir de varios factores, incluido `route_widener_mult`. Por tanto, el perfil solicita ahora `widenerMult=1.04f`, resultado del compromiso minimax entre el objetivo de foco (1.00) y la referencia de inmersión (1.08). Este valor multiplica la intensidad espacial publicada al plano de control; no es el ancho acústico final ni demuestra que la salida sea la matriz constante de la sección 1. El comportamiento de producción tiene memoria y respuesta dependiente de frecuencia.

Para afirmar que el perfil ejecuta exactamente $S\mapsto1.08S$ habría que cambiar/aislar el camino nativo y demostrarlo con pruebas específicas; esta documentación no declara que eso ya se haya hecho.

## 6. Validación de producción

1. Inspeccionar la ruta completa desde `OutputRoute.WIRED_AUX` hasta `StereoWidener::setWidth/process`, incluyendo factores combinados, prioridades HOA/WFS y suavizado.
2. Crear pruebas unitarias separadas para (a) la matriz ideal de referencia y (b) el DSP nativo real. No usar las pruebas matemáticas para afirmar que el DSP filtrado es idéntico.
3. Para el modelo ideal, probar mono puro invariante, Side puro escalado por 1.08, suma mono conservada y resultados finitos. La política minimax de ruta tiene su propia suite JVM y no se usa como sustituto de estas pruebas del DSP.
4. Para el DSP real, probar convergencia, transición del ancho, respuesta por frecuencia, graves, bloqueo de DC y ausencia de NaN/Inf. La tolerancia de float32 debe fijarse según el camino medido; no prometer 1 ULP antes de medir el código real.
5. Comparar AUX neutro y beta con el mismo programa y ganancia calibrada. Registrar true peak, RMS/LUFS, correlación L/R, señal mono, espectro de diferencia y posibles artefactos.
6. En el MHC-PZ1D, mantener geometría, volumen y ecualización constantes en Stereo/Sub OFF, Stereo/Sub ON, Surround/Sub OFF y Surround/Sub ON. Medir cada estado; una prueba ABX aleatorizada puede evaluar preferencia perceptual.

No se han medido la respuesta impulsional en la habitación, ganancia de satélites, crossover ni interacción Surround/subwoofer del equipo. No se afirma una mejora acústica verificada. Nunca conectar una salida amplificada de altavoz directamente al teléfono.

## 7. Métricas de evaluación

```text
rho = sum(L*R) / sqrt(sum(L^2)*sum(R^2) + eps)
mono = (Lout + Rout) / 2
peak = max(max(abs(Lout)), max(abs(Rout)))
interaction(f) = [D(f)-C(f)] - [B(f)-A(f)]
```

La correlación no es una puntuación de calidad por sí sola. Calcular diferencias entre capturas alineadas con referencia de nivel común. Expresar la interacción en dB solo para respuestas medidas válidas y fuera del piso de ruido.

## 8. Criterio de cierre

El modelo matemático queda cerrado. El perfil sigue siendo beta hasta que las pruebas del camino nativo y el build pasen, y la evaluación física confirme que no hay clipping ni degradación inaceptable. CI valida código y pruebas; no sustituye las mediciones físicas del Genezi.
