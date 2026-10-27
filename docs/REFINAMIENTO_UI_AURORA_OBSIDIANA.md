# Refinamiento integral de UI — Aurora Obsidiana

## Objetivo
Unificar la experiencia de IVANNA OMEGA SUPREME en la pantalla principal, los cinco espacios principales (CONTROL, BRAIN, ADAPTIVE, SPATIAL y SYSTEM) y las pantallas secundarias que reutilizan componentes compartidos. El refinamiento debe aumentar la jerarquía visual, la legibilidad y la sensación de precisión sin cambiar el comportamiento del DSP ni ocultar estados de telemetría.

## Lenguaje visual
- **Base OLED:** ObsidianVoid/ObsidianDeep como fondo; ObsidianSoft para superficies; ObsidianRaised para módulos elevados; ObsidianInset para campos hundidos.
- **Cristal técnico, no ruido visual:** bordes finos y consistentes, reflejo superior muy sutil, gradientes contenidos y radios de 18–20 dp en tarjetas principales.
- **Acentos semánticos:** cian para ruta/controles primarios, magenta para percepción/cerebro, ámbar para estado/diagnóstico y verde fosforescente para operación confirmada. El color no sustituye el texto del estado.
- **Tipografía de lectura primero:** títulos con tracking reducido para evitar recortes en móvil; cuerpo legible; monoespaciada reservada para etiquetas técnicas, unidades y telemetría.
- **Navegación:** dock inferior flotante con superficie elevada, contorno de cristal, área de toque cómoda y estado seleccionado claramente distinguible.
- **Tarjetas compartidas:** un mismo lenguaje para cabeceras de módulo, HubCard, GlassCard, StatusPill, controles y estadísticas.

## Principios de interacción
1. No cambiar una API de audio, callbacks, valores por defecto ni orden de inicialización por motivos visuales.
2. No presentar una ruta solicitada como ruta confirmada. La telemetría no disponible debe seguir apareciendo como no disponible.
3. Mantener navegación y callbacks existentes; el rediseño no puede convertir acciones reales en botones decorativos.
4. Evitar pulsos o animaciones continuas fuera de indicadores relevantes; el movimiento es informativo, no ornamental.
5. Usar contraste suficiente y textos con saltos naturales para pantallas estrechas. Evitar títulos de una línea que empujen el contenido fuera de pantalla.
6. Mantener los estados de consentimiento, permisos y diagnóstico explícitos.

## Alcance implementado
- ui/theme/IvannaTheme.kt: paleta elevada, superficies nuevas y tipografía más legible.
- ui/IvannaNavigation.kt: dock inferior de cristal y estilo compartido de cabeceras/tarjetas de los hubs SPATIAL y SYSTEM.
- ui/IvannaOmniComponents.kt: superficies GlassCard, controles de estado y etiquetas de estado refinadas; mantiene callbacks y valores DSP.
- Las pantallas que reutilizan estos componentes reciben el estilo común sin duplicar temas locales.

## Validación requerida
- Compilación Kotlin/Compose de Release.
- Tests host y verificaciones de cableado.
- Pruebas visuales en pantalla estrecha (Moto G85, modo oscuro), rotación y escalado de fuente.
- Smoke test de navegación en los cinco tabs y de apertura de pantallas secundarias.
- Escucha A/B al mismo nivel para confirmar que la actualización visual no altera la ruta de audio.
