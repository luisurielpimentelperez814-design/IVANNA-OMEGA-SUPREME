import os

files_to_check = [
    "IvannaFusionCore.hpp",
    "app/src/main/cpp/IvannaFusionCore.hpp",
    "native_kernel/IvannaFusionCore.hpp"
]

for f in files_to_check:
    if os.path.exists(f):
        with open(f, 'r') as file:
            content = file.read()
        
        new_content = content.replace("using IvannaFusionCore = IvannaFusionEngine;", "// using IvannaFusionCore = IvannaFusionEngine; // FIXED: Removed alias redefinition")
        
        with open(f, 'w') as file:
            file.write(new_content)
        print(f"Patched {f}")
