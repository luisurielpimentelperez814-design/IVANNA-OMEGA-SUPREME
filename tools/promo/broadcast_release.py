#!/usr/bin/env python3
"""
Módulo de propagación automática de lanzamientos para IVANNA OMEGA SUPREME.
Difunde el anuncio del nuevo release y el flyer promocional a través de:
- Telegram Bot API (Canales / Grupos)
- Discord Webhooks (Canales de anuncios)
- X / Twitter / Bluesky / Webhooks sociales
"""
import sys, os, json, urllib.request, urllib.parse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

def get_release_info():
    version = "v2.5.69"
    prop = ROOT / "magisk_module" / "module.prop"
    if prop.exists():
        for line in prop.read_text(encoding="utf-8").splitlines():
            if line.startswith("version="):
                version = line.split("=", 1)[1].strip()
    
    repo = os.environ.get("GITHUB_REPOSITORY", "luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME")
    release_url = f"https://github.com/{repo}/releases/tag/{version}"
    download_url = f"https://github.com/{repo}/releases/latest"
    web_url = f"https://luisurielpimentelperez814-design.github.io/IVANNA-OMEGA-SUPREME/"
    
    message = (
        f"🚀 Nuevo Release Publicado: IVANNA OMEGA SUPREME {version}\n\n"
        f"✦ Sistema Operativo de Supremacía Neuroacústica para Android (C++20 & ARM64 NEON).\n"
        f"✦ Sincronía A/V Cero-Desfase en Cine, Streaming y Música.\n"
        f"✦ Anti-Dolby CRNN nativo TFLite restaurado e íntegro.\n"
        f"✦ Doble ruta disponible: APK (sin root) y Módulo Magisk (root global).\n\n"
        f"🔗 Descarga el APK y Módulo: {download_url}\n"
        f"🌐 Web oficial y documentación: {web_url}\n"
        f"⭐ Apóyanos con una estrella o reporte de dispositivo en GitHub!"
    )
    return version, repo, release_url, web_url, message

def broadcast_telegram(version, message, flyer_path):
    token = os.environ.get("TELEGRAM_BOT_TOKEN")
    chat_id = os.environ.get("TELEGRAM_CHAT_ID")
    if not token or not chat_id:
        print("[TELEGRAM] Saltado (configura TELEGRAM_BOT_TOKEN y TELEGRAM_CHAT_ID en GitHub Secrets).")
        return
    
    url = f"https://api.telegram.org/bot{token}/sendPhoto"
    try:
        boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW"
        body = []
        body.append(f"--{boundary}".encode())
        body.append(b'Content-Disposition: form-data; name="chat_id"\r\n')
        body.append(b'\r\n' + chat_id.encode())
        
        body.append(f"--{boundary}".encode())
        body.append(b'Content-Disposition: form-data; name="caption"\r\n')
        body.append(b'\r\n' + message.encode('utf-8'))
        
        if flyer_path.exists():
            body.append(f"--{boundary}".encode())
            body.append(f'Content-Disposition: form-data; name="photo"; filename="{flyer_path.name}"'.encode())
            body.append(b'Content-Type: image/png\r\n')
            body.append(b'\r\n' + flyer_path.read_bytes())
            
        body.append(f"--{boundary}--\r\n".encode())
        payload = b"\r\n".join(body)
        
        req = urllib.request.Request(url, data=payload, headers={
            "Content-Type": f"multipart/form-data; boundary={boundary}"
        })
        with urllib.request.urlopen(req) as resp:
            res = json.loads(resp.read().decode())
            if res.get("ok"):
                print(f"[TELEGRAM] ✓ Flyer y anuncio enviados con éxito al canal {chat_id}.")
            else:
                print(f"[TELEGRAM] Error de API: {res}")
    except Exception as e:
        print(f"[TELEGRAM] Error al enviar: {e}")

def broadcast_discord(version, message, flyer_path, release_url):
    webhook_url = os.environ.get("DISCORD_WEBHOOK_URL")
    if not webhook_url:
        print("[DISCORD] Saltado (configura DISCORD_WEBHOOK_URL en GitHub Secrets).")
        return
        
    try:
        embed = {
            "title": f"⬡ IVANNA OMEGA SUPREME {version} — ¡Lanzamiento Oficial! ⬡",
            "description": message,
            "url": release_url,
            "color": 0x00F0FF,
            "footer": {
                "text": "IVANNA OMEGA SUPREME · GORE TNS"
            }
        }
        
        payload = {"embeds": [embed]}
        data = json.dumps(payload).encode("utf-8")
        req = urllib.request.Request(webhook_url, data=data, headers={
            "Content-Type": "application/json",
            "User-Agent": "IvannaReleaseBot"
        })
        with urllib.request.urlopen(req) as resp:
            print("[DISCORD] ✓ Anuncio publicado en Discord con éxito.")
    except Exception as e:
        print(f"[DISCORD] Error al publicar: {e}")

def broadcast_generic_webhook(version, message, release_url):
    webhook_url = os.environ.get("SOCIAL_BROADCAST_WEBHOOK_URL")
    if not webhook_url:
        print("[SOCIAL WEBHOOK] Saltado (configura SOCIAL_BROADCAST_WEBHOOK_URL para Zapier/Make/X/Slack).")
        return
    try:
        payload = {
            "event": "release_published",
            "version": version,
            "text": message,
            "url": release_url
        }
        data = json.dumps(payload).encode("utf-8")
        req = urllib.request.Request(webhook_url, data=data, headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req) as resp:
            print("[SOCIAL WEBHOOK] ✓ Evento enviado al webhook de automatización social.")
    except Exception as e:
        print(f"[SOCIAL WEBHOOK] Error al enviar: {e}")

def main():
    version, repo, release_url, web_url, message = get_release_info()
    flyer_path = ROOT / "docs" / "release_media" / "ivanna_promo_flyer.png"
    
    print(f"=== DIFUSIÓN DE RELEASE {version} ===")
    print(f"Repositorio: {repo}")
    print(f"Flyer: {flyer_path} (Existe: {flyer_path.exists()})")
    
    broadcast_telegram(version, message, flyer_path)
    broadcast_discord(version, message, flyer_path, release_url)
    broadcast_generic_webhook(version, message, release_url)
    print("=== FIN DE DIFUSIÓN ===")

if __name__ == "__main__":
    main()
