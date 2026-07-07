package com.ivanna.omega.core

/**
 * OmegaApplication — DEPRECATED WRAPPER.
 *
 * Historial: esta clase fue el primer Application del proyecto y contenía
 * solo `OmegaEngine.init(this)`. Luego se creó IVANNAApplication con toda
 * la orquestación real (DSPBridge, AudioEngine, AntiDolbyController,
 * OmegaDaemon, OmegaEngineBridge, GlobalEffectManager). El AndroidManifest.xml
 * fue actualizado en el FIX 2 de FIXES_CONECTIVIDAD.md para apuntar a
 * `.core.IVANNAApplication`, dejando esta clase como código muerto.
 *
 * REGLA DE ORO — no borramos, mejoramos:
 *   - Se preserva el símbolo público `OmegaApplication` para no romper
 *     ningún import externo, submódulo, o build variant que pudiera
 *     estar apuntando aquí en ramas/forks.
 *   - Ahora hereda de IVANNAApplication, así que si alguien lo referencia
 *     por accidente (por ejemplo, editando manualmente el Manifest o desde
 *     tests instrumentados), el flujo COMPLETO de inicialización se
 *     dispara exactamente igual que con IVANNAApplication — no queda
 *     una app a medio inicializar con silencio en el DSP.
 *   - `@Deprecated` marca la migración correcta para IDE + build tools;
 *     el AndroidManifest ya usa IVANNAApplication directamente.
 *
 * TODO (siguiente fase): eliminar por completo en v2.0, cuando se
 * confirme que ninguna herramienta externa depende de esta clase.
 */
@Deprecated(
    message = "Use IVANNAApplication. OmegaApplication solo existe como alias " +
              "para retro-compatibilidad — el AndroidManifest ya apunta a " +
              "IVANNAApplication (ver FIXES_CONECTIVIDAD.md FIX 2).",
    replaceWith = ReplaceWith(
        expression = "IVANNAApplication",
        imports = ["com.ivanna.omega.core.IVANNAApplication"]
    ),
    level = DeprecationLevel.WARNING
)
class OmegaApplication : IVANNAApplication()
