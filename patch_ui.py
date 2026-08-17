import os

def patch_file(path, replacements):
    with open(path, 'r') as f:
        content = f.read()
    for old, new in replacements:
        content = content.replace(old, new)
    with open(path, 'w') as f:
        f.write(content)
    print(f"Patched {path}")

# ControlTabScreen.kt
patch_file("app/src/main/java/com/ivanna/omega/ui/ControlTabScreen.kt", [
    ("IvannaNativeLib.nativeSetEQParams", "IvannaNativeLib.guardedNative(Unit) { IvannaNativeLib.nativeSetEQParams"),
    ("dsp.value.master)", "dsp.value.master) }"),
    ("if (enabled) IvannaNativeLib.nativeStartEvoThread()\n                else IvannaNativeLib.nativeStopEvoThread()", "if (enabled) IvannaNativeLib.guardedNative(Unit) { IvannaNativeLib.nativeStartEvoThread() }\n                else IvannaNativeLib.guardedNative(Unit) { IvannaNativeLib.nativeStopEvoThread() }")
])

# AdaptiveEngineScreen.kt
patch_file("app/src/main/java/com/ivanna/omega/ui/AdaptiveEngineScreen.kt", [
    ("IvannaNativeLib.nativeSetAdaptiveControls(", "IvannaNativeLib.guardedNative(Unit) { IvannaNativeLib.nativeSetAdaptiveControls("),
    ("100f\n                                )", "100f\n                                ) }"),
    ("1f\n                        )", "1f\n                        ) }"),
    ("IvannaNativeLib.nativeSetAdaptiveEngineEnabled(!enabled)", "IvannaNativeLib.guardedNative(Unit) { IvannaNativeLib.nativeSetAdaptiveEngineEnabled(!enabled) }")
])
