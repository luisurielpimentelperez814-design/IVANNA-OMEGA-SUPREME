# IVANNA-OMEGA-SUPREME — Fixes A · B · C · D
Base sincronizada: `main @ d795699` ("nav: tab BRAIN shows BrainScreen (#9)").
Regla de oro respetada: **no se borra nada**; sólo se desactiva código duplicado
muerto (con comentario explicando por qué) y se renombra el fantasma.

## Qué arregla

| # | Fix | Efecto real |
|---|-----|-------------|
| A | Duplicado `composable(IvannaRoute.ADAPTIVE)` (= `"adaptive"`) eliminado del NavHost | `AdaptiveEngineScreen` (L324) vuelve a ser el único destino de `"adaptive"` |
| B | Duplicado `composable(IvannaRoute.LAB)` (= `"lab"`) eliminado | `IvannaLabScreen` (L342) queda como destino único |
| C | **CRÍTICO** — nuevo `ui/ControlTabScreen.kt` con los 28 parámetros cableados; `MainScaffold` delega en él | Los 19 callbacks que caían en `{}` (anti-Dolby, presets, auto/omega mode, comp threshold/ratio, NHO, spatial angle/width/enabled, EVO, NPE bypass/harmonic/lateral-inhib/OHC/master-gain/AGC/flags/manifold, Phase Oracle) ahora mueven el motor de verdad |
| D | `ui/MainActivity.kt` → `ui/CognitiveDashboardActivity.kt` (clase renombrada, contenido íntegro) | Se acaba la ambigüedad con el launcher real `com.ivanna.omega.MainActivity`; `CognitiveDashboardScreen` se conserva |

### Detalle de C
`ControlTabScreen` replica exactamente el cableado que ya existía en el
`DashboardScreen` legacy de `MainActivity.kt` (que no se borra), pero dentro del
scaffold de tabs: DSP core → `dsp.pushToNative()`, NPE → `PiLstmBridge`,
spatial → `IvannaNativeLib` + `IvannaSpatialEngine`, anti-Dolby →
`AntiDolbyController` con su `onDspUpdate`, presets → `ProfilesLoader`,
auto/omega → `ParameterStore` + `OmegaEngineBridge.setIntensity`,
Phase Oracle → `nativeSetPhaseParameters`, adaptive mode/intensity →
`AudioStateManager` + `AdaptiveBackend.applyManualState`, voz → `VoiceProtectionManager`.

## Aplicar y pushear desde Termux (4 commits, uno por uno)

```bash
pkg install -y git python
cd ~
gh repo clone luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME || \
  git clone https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME.git
unzip -o IVANNA-OMEGA-ABCD.zip -d ~/abcd
bash ~/abcd/apply_ABCD.sh ~/IVANNA-OMEGA-SUPREME
cd ~/IVANNA-OMEGA-SUPREME
git log --oneline -4       # A, B, C, D
git push origin main
```

El script es idempotente: si un fix ya está aplicado, lo omite sin romper el resto.

## Verificación tras el build
1. Tab **CONTROL** → mover *EXCITER / EQ / WIDTH / COMP* debe cambiar audio (ya lo hacía).
2. Mover *NPE harmonic, OHC, AGC, spatial angle/width, Phase Oracle, anti-Dolby, preset* → **ahora también** (antes era silencio absoluto).
3. Navegar a `"adaptive"` y `"lab"` desde el dashboard legacy: abren pantalla real, no rebote a BRAIN.
4. Gradle: un solo `MainActivity` en el árbol.

## Seguridad
Revoca ya el token que pegaste en el chat (GitHub → Settings → Developer settings → Tokens).
Desde aquí no puedo hacer `git push` a tu repo externo; por eso va el script listo para Termux.
