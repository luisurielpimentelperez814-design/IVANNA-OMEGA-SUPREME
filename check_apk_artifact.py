import zipfile
import os

# Busca cualquier archivo zip descargado en el directorio actual
zips = [f for f in os.listdir('.') if f.endswith('.zip')]
if not zips:
    print("[-] No se encontró ningún archivo .zip en el directorio actual. Coloca el zip descargado aquí.")
    exit(1)

for zname in zips:
    print(f"\n[+] Analizando contenedor ZIP: {zname}")
    try:
        with zipfile.ZipFile(zname, 'r') as z:
            print("    Archivos dentro del ZIP contenedor:")
            for filename in z.namelist():
                info = z.getinfo(filename)
                print(f"      - {filename} ({info.file_size} bytes)")
                
                # Si dentro hay un APK, lo analizamos también
                if filename.endswith('.apk'):
                    apk_path = z.extract(filename)
                    print(f"    [+] Extrayendo APK interno: {apk_path}")
                    try:
                        with zipfile.ZipFile(apk_path, 'r') as apk_z:
                            print("        Estructura interna del APK:")
                            for apk_file in apk_z.namelist()[:10]: # Primeros 10 archivos
                                print(f"          * {apk_file}")
                            print("        [✓] El APK es un archivo ZIP válido.")
                    except zipfile.BadZipFile:
                        print(f"        [✗] ¡ERROR! El archivo APK interno '{filename}' está CORRUPTO o no es un ZIP válido.")
    except zipfile.BadZipFile:
        print(f"    [✗] ¡ERROR! El contenedor ZIP '{zname}' está corrupto.")

