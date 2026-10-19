<div align="center">

<img src="docs/release_media/ivanna_omega_hero.svg" alt="IVANNA OMEGA SUPREME" width="100%" />

# LÉAME — IVANNA OMEGA SUPREME

### Una guía clara para entender qué hace el proyecto y qué falta por comprobar

[Volver al README técnico](README.md) · [Ver compilaciones](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/actions) · [Ver versiones](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/releases)

</div>

---

## ¿Qué es?

IVANNA OMEGA SUPREME es un proyecto de software para Android que trabaja con audio digital. Reúne una aplicación con controles, un motor de procesamiento de sonido y herramientas para medir y representar un espacio de escucha.

La idea central es sencilla: **medir cuando sea posible, explicar qué se está haciendo y no dar por confirmado algo que no se ha comprobado.**

El proyecto está en desarrollo. Algunas funciones ya tienen código y pruebas automáticas; otras necesitan pruebas en teléfonos reales o hardware específico.

## ¿Qué hace cada parte?

<details open>
<summary><strong>1. Procesamiento de audio</strong></summary>

La aplicación controla un motor nativo escrito en C++. Este motor contiene herramientas para procesar el audio y trabajar con sonido espacial. La disponibilidad de cada función depende de cómo esté conectado el dispositivo y de qué ruta de audio se utilice.

</details>

<details open>
<summary><strong>2. Medición y ajuste de una sala</strong></summary>

El teléfono puede reproducir una secuencia de tonos y grabar la respuesta con su micrófono. Con esa información, el programa intenta estimar diferencias de nivel y diseñar una corrección.

El sistema puede comparar una medición inicial con otra después del ajuste. Si la medición no es fiable o no cumple el criterio de mejora programado, el código restaura la configuración anterior y registra la decisión.

**Importante:** el micrófono de un teléfono no es un instrumento de laboratorio. La ubicación del teléfono, el ruido, el volumen y los altavoces afectan el resultado. Una cifra calculada no garantiza que todas las personas perciban una mejora.

</details>

<details open>
<summary><strong>3. Vista geométrica 3D</strong></summary>

Existe un renderizador que dibuja una sala a partir de sus dimensiones y posiciones configuradas para el oyente y las fuentes de sonido. Puede producir una imagen de hasta 3840 × 2160 píxeles.

Esta vista es una representación de la geometría introducida. **No es todavía un escaneo automático del cuarto ni prueba que el sistema haya reconstruido la sala mediante Wi-Fi.**

</details>

<details open>
<summary><strong>4. Wi-Fi y contexto del entorno</strong></summary>

El proyecto contiene una política conservadora que puede pedir una nueva medición acústica cuando cambian ciertos indicios del entorno. La información Wi-Fi, por sí sola, no puede autorizar directamente cambios en el procesamiento del audio.

Las funciones avanzadas de CSI dependen de hardware y de un proveedor compatible. No se debe asumir que cualquier teléfono puede obtener esos datos.

</details>

## ¿Cómo sabemos si una nueva versión está lista?

Un archivo APK o ZIP que aparece en una publicación no es prueba suficiente. Para considerar una entrega lista, se debe comprobar:

- [ ] La compilación corresponde al último commit que se quiere entregar.
- [ ] Las pruebas automáticas terminan correctamente.
- [ ] El APK y el módulo Magisk se generaron desde esa misma revisión.
- [ ] Se verificaron arquitectura y hashes de los archivos.
- [ ] Se instalaron y probaron en un dispositivo compatible.
- [ ] Se comprobó la restauración de ajustes ante un resultado fallido.
- [ ] Los límites y las funciones que todavía faltan están documentados.

El estado más reciente de la compilación se consulta en [GitHub Actions](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/actions). Las [versiones publicadas](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/releases) pueden contener borradores o paquetes anteriores; revisa el commit de origen antes de instalar.

## ¿Puedo probarlo?

Sí, si cuentas con un dispositivo compatible y entiendes que se trata de un proyecto en desarrollo. Consulta primero el [README técnico](README.md) y las instrucciones de compilación. No instales módulos que requieran root sin comprender sus efectos y sin contar con una forma de recuperar tu dispositivo.

Si encuentras un error, abre un [reporte en GitHub](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/issues) e incluye el modelo del teléfono, la versión de Android, los pasos para reproducir el problema y los registros relevantes. No publiques contraseñas, tokens, grabaciones privadas ni datos personales.

## Qué no prometemos

Para mantener una descripción fiable, este proyecto no afirma que:

- mejore el sonido de forma universal en todos los teléfonos o audífonos;
- consiga latencia cero en todas las rutas de audio;
- funcione con cualquier proveedor o chipset CSI;
- la vista geométrica sea una reconstrucción real de la habitación;
- una compilación exitosa equivalga a una instalación probada;
- todos los módulos del repositorio estén conectados a la ruta de audio activa.

Estas funciones solo deben anunciarse como verificadas cuando exista evidencia apropiada: código conectado, pruebas reproducibles y, cuando corresponda, resultados obtenidos en hardware real.

## Documentación y contacto

- [README técnico](README.md) — arquitectura, compilación, pruebas y límites.
- [Ficha de ingeniería](docs/ENGINEERING.md) — decisiones técnicas y cómo verificarlas.
- [Registro de cambios](CHANGELOG.md) — historial de implementaciones y pendientes.
- [Código fuente](app/src/main/cpp/) — motor nativo.
- [Issues](https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/issues) — reportes y propuestas.

**Autor:** Luis Uriel Pimentel Pérez (GORE TNS).  
**Licencia declarada del repositorio:** Apache-2.0; consulta `LICENSE`, `NOTICE` y las licencias de los recursos de terceros.

El objetivo es construir un sistema serio y auditable: cada función debe poder localizarse en el código, cada prueba debe poder repetirse y cada limitación debe explicarse con claridad.
