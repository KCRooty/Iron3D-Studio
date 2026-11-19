"""Genera todos los iconos de Iron3D Studio desde assets/i3ds_logo_nuevo_1000.png.

Uso: python -I tools/make_icons.py [ruta_repo_web]
Escribe: assets/icon.png, assets/icon.ico, installer/assets/{setup_icon.ico,wizard_small.bmp,wizard_image.bmp},
src/ui/AppIconData.h (RGBA 64x64 para glfwSetWindowIcon) y, si se da la ruta de la web, sus copias.
"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = Image.open(os.path.join(root, "assets", "i3ds_logo_nuevo_1000.png")).convert("RGBA")

# Recorte cuadrado de la esfera (sin la sombra de contacto): centro (500, 470), radio con aro ~380.
CX, CY, RAD = 500, 470, 384
crop = src.crop((CX - RAD, CY - RAD, CX + RAD, CY + RAD))
m = Image.new("L", crop.size, 0)
ImageDraw.Draw(m).ellipse((0, 0, crop.size[0] - 1, crop.size[1] - 1), fill=255)
a = np.minimum(np.asarray(crop.getchannel("A")), np.asarray(m))
crop.putalpha(Image.fromarray(a))
big = crop.resize((1024, 1024), Image.LANCZOS)


def sized(n):
    return big.resize((n, n), Image.LANCZOS)


# PNG principal (README, repo)
big.save(os.path.join(root, "assets", "icon.png"))
sizes = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
big.resize((256, 256), Image.LANCZOS).save(os.path.join(root, "assets", "icon.ico"), sizes=sizes)
big.resize((256, 256), Image.LANCZOS).save(os.path.join(root, "installer", "assets", "setup_icon.ico"), sizes=sizes)

# Imagenes del asistente (Inno Setup): fondo oscuro del programa + logo
def wizard(w, h, logo, ytop):
    bg = Image.new("RGB", (w, h), (20, 22, 20))
    px = np.asarray(bg).astype(float)
    yy = np.linspace(0, 1, h)[:, None, None]
    px = px + np.array([6, 14, 6]) * (1 - yy) * 1.0  # degradado verdoso arriba
    bg = Image.fromarray(np.clip(px, 0, 255).astype(np.uint8))
    lg = sized(logo)
    bg.paste(lg, ((w - logo) // 2, ytop), lg)
    return bg


wizard(164, 314, 128, 60).save(os.path.join(root, "installer", "assets", "wizard_image.bmp"))
sm = Image.new("RGB", (55, 58), (255, 255, 255))
l = sized(48)
sm.paste(l, (3, 5), l)
sm.save(os.path.join(root, "installer", "assets", "wizard_small.bmp"))

# Icono de ventana embebido (64x64 RGBA, sin alfa premultiplicado)
px = np.asarray(sized(64)).reshape(-1, 4)
lines = []
for i in range(0, len(px), 16):
    lines.append("    " + ", ".join("0x%02X,0x%02X,0x%02X,0x%02X" % tuple(p) for p in px[i:i + 16]) + ",")
hdr = (
    "#pragma once\n// Generado por tools/make_icons.py desde el logo nuevo de I3DS. No editar a mano.\n"
    "namespace iron3d {\ninline constexpr int kAppIconSize = 64;\n"
    "inline constexpr unsigned char kAppIconRgba[64 * 64 * 4] = {\n" + "\n".join(lines) + "\n};\n}\n"
)
with open(os.path.join(root, "src", "ui", "AppIconData.h"), "w", newline="\n") as f:
    f.write(hdr)

# Web
if len(sys.argv) > 1:
    web = sys.argv[1]
    big.resize((160, 160), Image.LANCZOS).save(os.path.join(web, "assets", "projects", "iron3d-studio.webp"), quality=92)
    sized(32).save(os.path.join(web, "assets", "projects", "i3ds-icon-32.png"))
    sized(180).save(os.path.join(web, "assets", "projects", "i3ds-apple-touch.png"))
print("ok")
