# IVANNA-OMEGA-SUPREME

**Motor de Audio Espacial y Procesamiento Dinámico de Nueva Generación para Android**

IVANNA-OMEGA-SUPREME es un motor de audio avanzado diseñado con un objetivo claro: superar la calidad, transparencia y espacialidad de los procesadores comerciales como Dolby Atmos, operando de forma nativa en Android sin depender de las licencias o efectos de "caja negra" del sistema.

## 🎯 ¿Qué es el modo "Anti-Dolby"?

El término **Anti-Dolby** no es un ataque a la marca, sino una filosofía de diseño acústico. Los procesadores comerciales suelen aplicar una "firma de sonido" predefinida, comprimiendo la dinámica y alterando la fase para simular espacialidad. 

El modo **Anti-Dolby** de IVANNA toma un enfoque completamente distinto:
1. **Transparencia y Control:** No fuerza un sello de coloración. En su lugar, utiliza procesamiento dinámico adaptativo para respetar la mezcla original mientras expande el campo sonoro de forma natural.
2. **Independencia de Metadatos:** Mientras que las soluciones comerciales dependen de metadatos espaciales incrustados en el archivo (que a menudo se pierden o ignoran), IVANNA genera su propio campo 3D en tiempo real.
3. **Superación en su propio formato:** Cuando IVANNA detecta audio decodificado de formatos espaciales (como E-AC3/JOC), aplica un *downmix inteligente* y un *motor pseudo-espacial* que preserva la integridad del canal central y la altura acústica, sonando más amplio y definido que el procesador nativo de Dolby del dispositivo.

## ✨ Características Principales

### 🧠 Clasificación de Audio en Tiempo Real (YAMNet)
IVANNA "escucha" lo que estás reproduciendo. Utiliza un modelo de inteligencia artificial local para clasificar el audio en **Voz, Música o Graves** y adapta la cadena de efectos al instante:
* **Voz (Podcasts/Llamadas):** Reduce la expansión estéreo para mantener la claridad y el anclaje central, aplicando un realce quirúrgico en las frecuencias de inteligibilidad (2-4 kHz).
* **Música:** Activa el motor pseudo-espacial y el excitador armónico para dar calidez y amplitud.
* **Graves:** Aísla las frecuencias sub-bajas (<120Hz) para aplicar excitación de armónicos sin ensuciar los medios.

### 🌌 Motor Pseudo-Espacial (HRTF Ligero)
Crea la sensación de "altura" y "profundidad" sin metadatos. Aplica funciones de transferencia relacionadas con la cabeza (HRTF) de 128 taps, micro-retrasos (0.3ms) y filtros *shelving* en frecuencias altas (>8kHz) exclusivamente al contenido no-vocal, engañando al cerebro para percibir un escenario sonoro tridimensional.

### 🎛️ Cadena de Procesamiento Dinámico
* **Downmix Inteligente:** Si el sistema recibe un stream multicanal, IVANNA intercepta la señal y realiza un downmix matemático (L+R+0.7C) antes de que el sistema operativo lo aplaste, preservando la energía del canal central.
* **Preset "Anti-Dolby":** Compresor óptico lento (2:1), excitador de armónicos pares, expansión estéreo controlada y reverberación que solo actúa en la "cola" de los transitorios para no embarrar la mezcla.
* **Limiter Hard-Clip:** Protección absoluta a -0.1 dBFS para evitar cualquier distorsión digital o clipping en los altavoces del dispositivo.

## 📱 Modos de Instalación

IVANNA se adapta a tu nivel de acceso al dispositivo:

* **Modo Root (Magisk):** Se instala como un módulo de sistema. Intercepta el servidor de audio de Android (`audioserver`) para procesar toda la salida de audio del dispositivo con latencia casi nula a nivel de kernel.
* **Modo No-Root:** Funciona como una aplicación independiente que utiliza la API nativa `AudioTrack` y `DynamicsProcessing` de Android para procesar el audio localmente sin necesidad de permisos de superusuario.

## 🚀 Instalación

**Para usuarios con Root (Recomendado):**
1. Descarga el archivo `.zip` del módulo desde la sección de [Releases](../../releases).
2. Abre la aplicación Magisk.
3. Ve a la pestaña de Módulos e "Instalar desde almacenamiento".
4. Selecciona el zip de IVANNA-OMEGA-SUPREME y reinicia.

**Para usuarios sin Root:**
1. Descarga el archivo `.apk` desde la sección de [Releases](../../releases).
2. Instálalo como una aplicación normal.
3. Abre la app y activa el procesamiento.

---
*IVANNA-OMEGA-SUPREME está diseñado para audiófilos, entusiastas del cine en casa y usuarios que exigen el máximo rendimiento acústico de sus dispositivos Android.*
