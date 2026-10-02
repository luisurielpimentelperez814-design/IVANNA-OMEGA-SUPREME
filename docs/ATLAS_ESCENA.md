# ATLAS-ESCENA 12D — Especificación e Implementación de Singularidad Acústica

## 1. Visión General de Arquitectura

El subsistema **Atlas-Escena 12D** convierte las propiedades acústicas y musicales medibles en tiempo real en una reconstrucción física tridimensional continua de sala, objetos binaurales y estructura armónica, operando tanto **con Root** (`libomega_effect.so` en `audioserver`) como **sin Root** (`libivanna_omega.so` en las rutas Oboe / AudioTrack de la APK) desde el primer segundo de reproducción.

---

## 2. Vector de Características Adimensionales 12D (`MusicFeatureExtractor`)

Todas las componentes $f \in \mathbb{R}^{12}$ son **invariantes al nivel de ganancia** y a la **frecuencia de muestreo** $f_s \in \{44100, 48000, 96000\}\text{ Hz}$ gracias a coeficientes exactos $1 - e^{-2\pi f_c / f_s}$ y partición complementaria de 5 bandas ($\text{Sub } 20\text{–}80\text{ Hz}$, $\text{Cuerpo } 80\text{–}400\text{ Hz}$, $\text{Definición } 400\text{–}3000\text{ Hz}$, $\text{Presencia } 3\text{–}8\text{ kHz}$, $\text{Aire } 8\text{–}20\text{ kHz}$):

| Índice | Nombre | Definición Matemática |
|---|---|---|
| $f_0$ | `bassRatio` | $(E_{\text{sub}} + E_{\text{low}}) / E_{\text{total}}$ |
| $f_1$ | `trebleRatio` | $E_{>2\text{ kHz}} / E_{\text{total}}$ |
| $f_2$ | `crest24` | $\text{clamp}(\text{crestDb} / 24, 0, 1)$ |
| $f_3$ | `stereoWidth` | $\text{clamp}(1 - \rho_{LR}, 0, 1)$ (con intrínsecos SIMD ARM64 NEON `accumLR`) |
| $f_4$ | `transientRate` | Densidad de onsets normalizada a $f_s$ |
| $f_5$ | `density` | Proporción de muestras activas sobre umbral relativo al RMS |
| $f_6$ | `presenceRatio` | $E(3\text{–}8\text{ kHz}) / E_{\text{total}}$ |
| $f_7$ | `airRatio` | $E(8\text{–}20\text{ kHz}) / E_{\text{total}}$ |
| $f_8$ | `flatness1m` | $1 - \text{SFM}$ (1 menos razón media geométrica / aritmética de las 5 bandas) |
| $f_9$ | `onsetRegularity` | $1 - \text{CV}(\text{IOI})$ sobre intervalos entre onsets en segundos |
| $f_{10}$ | `lraProxy12` | $\text{clamp}(\sigma(\text{loudness}_{\text{st}}) / 12\text{ dB}, 0, 1)$ |
| $f_{11}$ | `sideMid` | $E_S / (E_M + E_S + \epsilon)$ |

---

## 3. Inferencia Bayesiana 12D y Mezcla Continua (`StyleBlender`)

- **Verosimilitud Gaussiana Diagonal con Normalización Log-Determinante**:
  $$\ell_k(f) = \ln \pi_k - \frac{1}{2}\sum_{i=0}^{11}\left(\frac{f_i - \mu_{ki}}{\sigma_{ki}}\right)^2 - \sum_{i=0}^{11}\ln\sigma_{ki}$$
- **Filtro Bayesiano Temporal ($\tau = 2.0\text{ s}$) e Histéresis de 3 Pasos ($\Delta p > 0.12$)**:
  Evita el parpadeo entre estilos; en arranque en frío las primeras 3 ventanas se evalúan cada $0.5\text{ s}$ (identificación $\le 1.5\text{ s}$).
- **Compuerta de Confianza `smoothstep(0.35, 0.60, conf)`**:
  Con $\text{conf} \le 0.35$ ($\text{gate} = 0$), la salida es **identidad neutral exacta bit-a-bit**.

---

## 4. Motores Físicos Conectados por `SceneTargetBus` (Triple Buffer Wait-Free)

1. **`LateReverbSuppressor` (`spatial/LateReverbSuppressor.hpp`, M5)**:
   Supresión estadística de cola difusa $> 50\text{ ms}$ (Lebart & Habets 2001) con partición complementaria de 3 polos a $250\text{ Hz}$ ($18\text{ dB/oct}$), dejando intactos el sub-grave ($< 0.08\text{ dB}$ a $60\text{ Hz}$) y el ataque directo ($0\text{ muestras}$ de latencia añadida).
2. **`PhysicalEarlyReflections` (`spatial/RoomProjectionEngine.hpp`, M7)**:
   6 fuentes imagen especulares de 1er orden (paredes izquierda/derecha/frontal/trasera, suelo y techo) con coeficiente de reflexión de Sabine-Eyring y panorámica binaural azimutal.
3. **`ObjectSpatialRenderer` (`spatial/ObjectSpatialRenderer.hpp`, M8)**:
   Retardo interaural esférico exacto de Woodworth $|\text{ITD}(\theta)| = \frac{a}{c}(|\theta| + \sin|\theta|)$ con interpolación fraccionaria lineal libre de clicks y filtro de sombra craneal de Brown-Duda ($f_c = 1.5\text{ kHz}$).
4. **`ChebHarmonicShaper` (`dsp/ChebHarmonicShaper.hpp`, M9)**:
   Síntesis armónica ortogonal con $T_2^*(u)$ (2º armónico par, calidez de triodo) y $T_3(u)$ (3er armónico impar, ataque de cinta), atenuación Anti-IMD por $f_8$ y bloqueador DC de 1er orden a $15\text{ Hz}$.
5. **Guarda Cibernética de Realismo M10 (`SceneTargetBus::updateRealismM10`)**:
   Evalúa en cada bloque $C_t$ (preservación de transitorios), $C_s$ (coherencia espacial IACC), $C_d$ (factor de cresta) y $Q = \max(0.05, C_t^{0.4} C_s^{0.3} C_d^{0.3})$. Si $C_t < 0.85$ o $\rho_{\text{out}} < -0.20$, reduce automáticamente la intensidad un $30\%$ ($\times 0.70$) con rampa suave de $100\text{ ms}$.
