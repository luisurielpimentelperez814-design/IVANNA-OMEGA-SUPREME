#!/data/data/com.termux/files/usr/bin/bash
set -e

echo "== Fix ProfileSelector import =="
grep -q "androidx.compose.ui.draw.clip" app/src/main/java/com/ivanna/omega/ui/ProfileSelector.kt || \
sed -i '/import androidx.compose.ui.Modifier/a import androidx.compose.ui.draw.clip' \
app/src/main/java/com/ivanna/omega/ui/ProfileSelector.kt


echo "== Fix ProfilesLoader expression body =="
sed -i 's/return emptyList()/emptyList()/g' \
app/src/main/java/com/ivanna/omega/audio/ProfilesLoader.kt


echo "== Add NativeLib declarations if missing =="
grep -q "nativeSetAdaptiveEngineEnabled" app/src/main/java/com/ivanna/omega/core/IvannaNativeLib.kt || \
sed -i '/external fun nativeSetAdaptiveControls/a\
\
    external fun nativeSetAdaptiveEngineEnabled(enabled: Boolean)\
\
    external fun nativeSetEQParams(low: Float, mid: Float, high: Float, master: Float)' \
app/src/main/java/com/ivanna/omega/core/IvannaNativeLib.kt


echo "== Add MasterBar callbacks defaults =="
sed -i '/internal fun MasterBar(/,/)/{
s/onOpenProfiles: () -> Unit = {},//
s/onOpenMagisk: () -> Unit = {},//
}' app/src/main/java/com/ivanna/omega/ui/IvannaOmniComponents.kt || true


echo "== Check changes =="
git diff --stat

echo "Listo. Revisar antes de commit."
