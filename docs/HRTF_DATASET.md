# Pipeline del dataset HRTF medido

Este documento describe cómo se genera, se instala y se **verifica** el dataset
HRTF que usa el motor espacial de Ivanna Omega.

Importa entender una cosa antes de nada: el renderizador tiene dos caminos
posibles y elige solo. Si encuentra un dataset medido válido lo usa; si no,
cae a un HRTF sintético (modelo Woodworth de ITD + head-shadow). Los dos
suenan, pero no suenan igual. Todo este pipeline existe para que el camino
medido esté realmente vivo en producción y para que puedas **comprobarlo**,
en vez de suponerlo.

---

## 1. Generar un dataset real (IHR1) desde un SOFA

Si tienes una medida SOFA (CIPIC, HUTUBS, KEMAR, ARI…):

```bash
python3 tools/hrtf/sofa_to_ihr1.py subject_003.sofa -o hrtf_dataset.ihr1
```

Dependencias: `numpy` y **uno** de estos lectores — `sofar`, `netCDF4` o
`h5py`. El script los prueba en ese orden (un `.sofa` es NetCDF4, que por
dentro es HDF5, así que `h5py` basta):

```bash
pip install numpy h5py
```

### Opciones que importan

| Flag | Default | Para qué sirve |
|---|---|---|
| `--target-sr` | `48000` | Sample rate de salida. **No lo bajes a la ligera** (ver aviso abajo). |
| `--ir-len` | `512` | Muestras por IR. Límite duro del cargador: `8192`. |
| `--max-elev` | `5.0` | Descarta medidas fuera de ±5° de elevación (el motor es horizontal). |
| `--az-round` | `1.0` | Redondeo del azimut en grados, para agrupar medidas. |
| `--no-dedupe-azimuth` | *(off)* | Desactiva quedarse con una sola medida por azimut. No recomendado. |
| `--flip-azimuth` | *(off)* | Invierte el signo del azimut si la convención del SOFA está al revés. |

> **Aviso — el resampleo no es opcional.**
> El cargador C++ (`spatial/synthetic_hrtf.hpp`) **lee** el campo `sampleRate`
> de la cabecera pero **nunca lo usa**: reproduce los HRIR al sample rate al
> que esté corriendo el motor. CIPIC está medido a 44100 Hz y el efecto se
> inicializa a 48000 Hz, así que un volcado directo sonaría ~8.8 % rápido: el
> ITD se encoge y los notches espectrales —que son justo las pistas de
> elevación— se desplazan hacia arriba. Por eso el resampleo se hace **en la
> conversión** y no en tiempo real. Si generas el dataset para un motor que
> corre a otro SR, pásalo con `--target-sr`.

> **Aviso — una medida por azimut.**
> `generateFromDataset()` interpola entre los dos azimuts adyacentes de la
> lista ordenada. Con `--max-elev 5` el CIPIC deja pasar varias medidas casi
> al mismo azimut pero de elevaciones distintas (80.00, 80.05, 80.19…).
> Interpolar entre ellas mezcla HRIR de elevaciones diferentes dentro de poco
> más de un grado → filtrado en peine e imagen inestable al girar la cabeza.
> El de-duplicado está **activo por defecto** y conserva, por cada azimut, la
> medida de |elevación| mínima.

---

## 2. Generar un dataset de prueba (sin dependencias externas)

Para desarrollo o CI, donde no quieres arrastrar `h5py` ni un SOFA de 2 MB:

```bash
python3 tools/hrtf/make_test_ihr1.py
```

Produce un `hrtf_dataset.ihr1` determinista y verificable (13 azimuts,
`IR_LEN=512`, `SR=48000`, ITD Woodworth + head-shadow). Sirve para validar
que la cadena de carga funciona; **no** sustituye a una medida real.

---

## 3. Instalación vía Magisk (producción)

El módulo embarca su propio dataset y lo despliega en la instalación:

```
magisk_module/system/etc/ivanna_omega/hrtf_dataset.ihr1   (embarcado)
                    ↓  customize.sh
/data/adb/ivanna_omega/hrtf_dataset.ihr1                  (0644 root:root)
```

Esa ruta destino no es arbitraria: es exactamente la que pide
`omega_effect.cpp` en `loadCustomHrtf()`.

> **Regla anti-sobrescritura.** Si en `/data/adb/ivanna_omega/` ya hay un
> `.ihr1` **no vacío**, `customize.sh` **no lo pisa** y lo dice en la consola
> de instalación (`HRTF dataset ya presente — preservando custom del
> usuario`). Consecuencia práctica: reinstalar el módulo **no** te va a
> devolver el dataset embarcado si ya tienes uno tuyo. Para volver al de
> fábrica hay que borrar el archivo primero.

### Reemplazarlo por uno tuyo

Con el módulo ya instalado, como root:

```bash
su -c "cp /sdcard/Download/hrtf_dataset.ihr1 /data/adb/ivanna_omega/hrtf_dataset.ihr1 \
       && chmod 0644 /data/adb/ivanna_omega/hrtf_dataset.ihr1 \
       && chown 0:0 /data/adb/ivanna_omega/hrtf_dataset.ihr1"
```

El motor lo carga en la **próxima sesión de audio** (`EFFECT_CMD_SET_CONFIG`),
no hace falta reinstalar el módulo ni reiniciar. Basta con parar y volver a
lanzar la reproducción.

---

## 4. Verificar que la ruta medida está activa

Este es el paso que la mayoría se salta y por el que luego nadie sabe qué está
sonando. El tag de logcat es **`IvannaOmegaEffect`**:

```bash
adb logcat -s IvannaOmegaEffect:V
```

**Dataset medido activo** (lo que quieres ver):

```
I/IvannaOmegaEffect: Custom HRTF dataset loaded from
                     /data/adb/ivanna_omega/hrtf_dataset.ihr1 (measured path ACTIVE)
```

**Fallback sintético** (algo falló):

```
W/IvannaOmegaEffect: Failed to load custom HRTF dataset from
                     /data/adb/ivanna_omega/hrtf_dataset.ihr1 (errno=2,
                     No such file or directory). Falling back to SYNTHETIC HRTF.
```

Cómo leer el `errno`:

| Mensaje | Significado | Solución |
|---|---|---|
| `errno=2, No such file or directory` | El archivo no está | Reinstala el módulo o cópialo a mano (§3) |
| `errno=13, Permission denied` | Permisos o SELinux | `chmod 0644` + `chown 0:0` |
| `errno=0, file readable but not a valid IHR1 dataset` | El archivo existe y se lee, pero la cabecera no valida | Regenéralo (§1); revisa los límites de §5 |

Si **no aparece ninguna de las dos líneas**, el efecto no llegó a recibir
`EFFECT_CMD_SET_CONFIG`: el problema está antes, en el enganche del
`omega_effect` a la sesión de audio, no en el HRTF.

---

## 5. Formato binario IHR1

Little-endian, sin padding:

```
[4B  magic  "IHR1"]
[i32 numDirs]
[i32 irLen]
[i32 sampleRate]          ← se lee pero el motor NO lo usa (ver §1)
  por cada dirección (numDirs veces):
    [f32 azimuthDeg]
    [f32 × irLen  IR oído izquierdo]
    [f32 × irLen  IR oído derecho]
```

Validación del cargador — si algo de esto no se cumple, `loadDatasetFromFile()`
devuelve `false` y el motor cae al sintético **sin** tocar el estado previo:

* magic exactamente `IHR1`
* `numDirs > 0` y `numDirs <= 1024`
* `irLen > 0` y `irLen <= 8192`
* el archivo debe contener los `numDirs × (1 + 2·irLen)` floats completos

Tamaño resultante: `16 + numDirs × (4 + 8 × irLen)` bytes.

Las direcciones se ordenan por azimut al cargarse, y el renderizado
interpola linealmente entre los dos azimuts adyacentes.
