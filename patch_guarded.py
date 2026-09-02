import os
import re

f = "./app/src/main/java/com/ivanna/omega/core/IvannaNativeLib.kt"
with open(f, 'r') as file:
    content = file.read()

guard_code = """
    inline fun <T> guardedNative(fallback: T, block: () -> T): T {
        return try {
            if (!isLoaded) {
                Log.e("IvannaNativeLib", "guardedNative: native library not loaded. Returning fallback.")
                return fallback
            }
            block()
        } catch (e: UnsatisfiedLinkError) {
            Log.e("IvannaNativeLib", "guardedNative: UnsatisfiedLinkError - ${e.message}", e)
            fallback
        } catch (e: Throwable) {
            Log.e("IvannaNativeLib", "guardedNative: Exception - ${e.message}", e)
            fallback
        }
    }
"""

if "guardedNative" not in content:
    # Insert it right after val isLoaded: Boolean get() = loaded
    content = content.replace("val isLoaded: Boolean get() = loaded", "val isLoaded: Boolean get() = loaded\n" + guard_code)
    
    with open(f, 'w') as file:
        file.write(content)
    print("Patched IvannaNativeLib.kt")
else:
    print("guardedNative already exists in IvannaNativeLib.kt")
