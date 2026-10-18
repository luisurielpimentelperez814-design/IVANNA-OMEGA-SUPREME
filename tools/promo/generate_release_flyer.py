#!/usr/bin/env python3
"""
Generador automático de Flyer Promocional para IVANNA OMEGA SUPREME.
Produce un arte vertical 1080x1350 optimizado para redes (X, LinkedIn, Telegram, Instagram).
"""
import sys, os, math
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = Path(__file__).resolve().parents[2]

def get_version():
    if len(sys.argv) > 1 and sys.argv[1]:
        return sys.argv[1]
    prop = ROOT / "magisk_module" / "module.prop"
    if prop.exists():
        for line in prop.read_text(encoding="utf-8").splitlines():
            if line.startswith("version="):
                return line.split("=", 1)[1].strip()
    return "v2.5.69"

def generate_flyer(version, out_path):
    W, H = 1080, 1350
    bg = Image.new("RGB", (W, H), (7, 12, 27))
    p = bg.load()
    
    # Gradiente radial cian/azul oscuro
    for y in range(H):
        for x in range(W):
            d = ((x - W * 0.5) ** 2 / (W * 0.8) ** 2 + (y - H * 0.35) ** 2 / (H * 0.8) ** 2) ** 0.5
            glow = max(0.0, 1.0 - d)
            p[x, y] = (int(7 + 4 * glow), int(12 + 14 * glow), int(27 + 36 * glow))
            
    d = ImageDraw.Draw(bg)
    
    # Cuadrícula acústica de fondo
    for x in range(0, W, 54):
        d.line((x, 0, x, H), fill=(16, 32, 56), width=1)
    for y in range(0, H, 54):
        d.line((0, y, W, y), fill=(16, 32, 56), width=1)
        
    # Rutas de fuentes compatibles
    font_path = "/nix/store/xbs17gmksi0pljxcs4l6gshklzpmv8gr-dejavu-fonts-2.37/share/fonts/truetype/DejaVuSans.ttf"
    bold_path = "/nix/store/xbs17gmksi0pljxcs4l6gshklzpmv8gr-dejavu-fonts-2.37/share/fonts/truetype/DejaVuSans-Bold.ttf"
    if not os.path.exists(font_path):
        import matplotlib.font_manager as fm
        fonts = [f for f in fm.findSystemFonts() if "DejaVu" in f and "Bold" in f]
        bold_path = fonts[0] if fonts else None
        regular = [f for f in fm.findSystemFonts() if "DejaVu" in f and "Bold" not in f]
        font_path = regular[0] if regular else bold_path
        
    def F(p, s):
        try:
            return ImageFont.truetype(p, s)
        except Exception:
            return ImageFont.load_default()

    # Onda de audio holográfica con brillo
    wave = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    wd = ImageDraw.Draw(wave)
    pts = []
    for x in range(40, W - 40):
        y = int(500 + 44 * math.sin(x / 72.0) + 16 * math.sin(x / 24.0))
        pts.append((x, y))
    wd.line(pts, fill=(0, 238, 255, 200), width=4)
    for offset, color in [(14, (96, 72, 255, 120)), (-12, (0, 238, 255, 90))]:
        pts2 = [(x, y + offset) for x, y in pts]
        wd.line(pts2, fill=color, width=2)
    wave = wave.filter(ImageFilter.GaussianBlur(1.5))
    bg = Image.alpha_composite(bg.convert("RGBA"), wave)
    d = ImageDraw.Draw(bg)
    
    # Badge superior de versión
    badge = (54, 54, 430, 114)
    d.rounded_rectangle(badge, radius=24, fill=(11, 28, 50, 255), outline=(0, 238, 255, 255), width=2)
    d.text((78, 70), f"NUEVO RELEASE  •  {version}", font=F(bold_path, 22), fill=(98, 245, 255))
    
    # Título principal
    d.text((54, 164), "IVANNA OMEGA", font=F(bold_path, 66), fill=(245, 250, 255), stroke_width=1, stroke_fill=(0, 238, 255, 70))
    d.text((54, 244), "SUPREME", font=F(bold_path, 66), fill=(0, 238, 255), stroke_width=1, stroke_fill=(98, 72, 255, 70))
    
    # Subtítulo y propuesta de valor
    d.text((58, 342), "MOTOR DE AUDIO C++20 · DSP TIEMPO REAL", font=F(bold_path, 26), fill=(120, 235, 255))
    d.text((58, 386), "Sincronía A/V Cero-Desfase en Cine, Streaming y Música.", font=F(font_path, 24), fill=(210, 225, 244))
    
    # Disco acústico holográfico central
    cx, cy = 540, 690
    for rad, col, width in [(175, (0, 229, 255), 3), (142, (99, 72, 255), 3), (108, (28, 213, 212), 2)]:
        d.ellipse((cx - rad, cy - rad, cx + rad, cy + rad), outline=col, width=width)
    for i in range(5):
        r = 75 - i * 11
        d.arc((cx - r, cy - r, cx + r, cy + r), start=205 + i * 9, end=335 - i * 8, fill=(0, 230 - i * 20, 255), width=5)
    d.text((cx, cy + 8), "IVANNA", font=F(bold_path, 32), fill=(245, 250, 255), anchor="mm")
    
    # Tarjetas de pilares técnicos
    card_y = 910
    cards = [
        ("DSP C++20 NEON", "Baja latencia (<0.1ms)"),
        ("RUTA A: APK", "Sin root para todos"),
        ("RUTA B: ROOT", "Módulo Magisk Global")
    ]
    for i, (title, sub) in enumerate(cards):
        x = 54 + i * 330
        d.rounded_rectangle((x, card_y, x + 310, card_y + 124), radius=18, fill=(15, 25, 48), outline=(36, 75, 105), width=2)
        d.text((x + 18, card_y + 22), title, font=F(bold_path, 20), fill=(98, 245, 255))
        d.text((x + 18, card_y + 66), sub, font=F(font_path, 19), fill=(226, 236, 250))
        
    # Garantía y verificación
    d.text((54, 1076), "✓ 406/406 pruebas unitarias pasadas en CI (ASan / UBSan)", font=F(font_path, 19), fill=(35, 240, 154))
    d.text((54, 1114), "✓ Anti-Dolby CRNN TFLite íntegro y validado con 0 errores", font=F(font_path, 19), fill=(0, 238, 255))
    d.text((54, 1152), "✓ Buscamos probadores para reporte en dispositivos reales", font=F(font_path, 19), fill=(195, 210, 231))
    
    # Botón Call to Action
    d.rounded_rectangle((54, 1206, 1026, 1296), radius=22, fill=(0, 210, 226), outline=(93, 250, 255), width=2)
    d.text((540, 1251), "DESCARGA GRATIS EL APK Y MÓDULO EN GITHUB", font=F(bold_path, 23), fill=(4, 18, 32), anchor="mm")
    d.text((540, 1324), "github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME", font=F(font_path, 16), fill=(144, 165, 197), anchor="mm")
    
    out_path = Path(out_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    bg.convert("RGB").save(str(out_path), optimize=True)
    print(f"[FLYER GENERATED] {out_path} ({out_path.stat().st_size} bytes)")

if __name__ == "__main__":
    v = get_version()
    # Generar en dos ubicaciones: la genérica y la fechada/versionada
    flyer_canonical = ROOT / "docs" / "release_media" / "ivanna_promo_flyer.png"
    flyer_versioned = ROOT / "docs" / "release_media" / f"ivanna_promo_flyer_{v}.png"
    generate_flyer(v, flyer_canonical)
    generate_flyer(v, flyer_versioned)
