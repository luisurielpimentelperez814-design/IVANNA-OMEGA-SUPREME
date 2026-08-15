import os

f = "app/src/main/cpp/omega_effect.cpp"
if os.path.exists(f):
    with open(f, 'r') as file:
        content = file.read()
    
    # We need to change instantiations of IvannaFusionCore to IvannaFusionEngine
    new_content = content.replace("new IvannaFusionCore", "new IvannaFusionEngine")
    
    with open(f, 'w') as file:
        file.write(new_content)
    print(f"Patched {f}")
