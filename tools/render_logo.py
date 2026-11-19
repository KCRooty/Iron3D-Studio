import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont

OUT = sys.argv[1]
SS = 2                     # supersampling
W = 1000 * SS
R = 0.36 * W               # radio de la esfera
CX, CY = 0.5 * W, 0.47 * W


def blur(a, sigma):
    """Gaussiana por FFT (sin scipy)."""
    h, w = a.shape
    fy = np.fft.fftfreq(h)[:, None]
    fx = np.fft.rfftfreq(w)[None, :]
    g = np.exp(-2 * (np.pi ** 2) * (sigma ** 2) * (fx ** 2 + fy ** 2))
    return np.fft.irfft2(np.fft.rfft2(a) * g, s=a.shape)


def norm(v):
    return v / np.maximum(np.linalg.norm(v, axis=-1, keepdims=True), 1e-9)


def dot(a, b):
    return np.sum(a * b, axis=-1, keepdims=True)


def smooth(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


ys, xs = np.mgrid[0:W, 0:W].astype(np.float64)
px = (xs - CX) / R
py = -(ys - CY) / R
rr2 = px ** 2 + py ** 2
inside = rr2 < 1.0
aa = np.clip((1.0 - np.sqrt(rr2)) * R * 0.5, 0, 1)  # borde suave (~2 px del tamano final)
pz = np.sqrt(np.clip(1 - rr2, 0, 1))
N = np.stack([px, py, pz], -1)
V = np.array([0, 0, 1.0])

# ---------- texto como campo de altura, proyectado sobre la esfera ----------
tw = int(W)
img = Image.new("L", (tw, tw), 0)
d = ImageDraw.Draw(img)
font = ImageFont.truetype("C:/Windows/Fonts/ariblk.ttf", int(0.27 * W))
text = "I3DS"
bb = d.textbbox((0, 0), text, font=font)
tx = (tw - (bb[2] - bb[0])) / 2 - bb[0]
ty = (tw - (bb[3] - bb[1])) / 2 - bb[1]
d.text((tx, ty), text, font=font, fill=255)
mask = np.asarray(img, dtype=np.float64) / 255.0

# proyeccion esferica: lon/lat -> plano de texto
lon = np.arcsin(np.clip(px / np.maximum(np.sqrt(1 - py ** 2), 1e-6), -1, 1))
lat = np.arcsin(np.clip(py, -1, 1))
# el plano de texto cubre +-0.78 del radio; escala para que 1 rad = R*0.98 px
lon = np.nan_to_num(lon); lat = np.nan_to_num(lat)
sx = CX + lon * R * 0.98
sy = CY - lat * R * 0.98 + 0.0


def sample(a, x, y):
    x = np.clip(x, 0, a.shape[1] - 1.001)
    y = np.clip(y, 0, a.shape[0] - 1.001)
    x0 = x.astype(int)
    y0 = y.astype(int)
    fx = x - x0
    fy = y - y0
    return (a[y0, x0] * (1 - fx) * (1 - fy) + a[y0, x0 + 1] * fx * (1 - fy)
            + a[y0 + 1, x0] * (1 - fx) * fy + a[y0 + 1, x0 + 1] * fx * fy)


tmask = sample(mask, sx, sy) * inside
# bisel redondeado: altura = mascara suavizada
height = blur(tmask, 0.012 * R) * 1.0 + blur(tmask, 0.004 * R) * 0.6
gy, gx = np.gradient(height)
bump = 0.045 * R  # fuerza del relieve
Nt = norm(N + np.stack([-gx * bump, gy * bump, np.zeros_like(gx)], -1) * 1.0)
tm = np.clip(tmask, 0, 1)[..., None]
sh_t = blur(tmask, 0.03 * R)  # sombra del texto sobre la esfera (desplazada)
sh_t = np.roll(np.roll(sh_t, int(0.022 * R), axis=0), int(0.012 * R), axis=1)

# ---------- entorno (reflejos) ----------
def env(dirv):
    y = dirv[..., 1:2]
    x = dirv[..., 0:1]
    z = dirv[..., 2:3]
    sky = np.array([0.80, 0.92, 1.00]) * (0.45 + 0.55 * smooth(-0.1, 1.0, y))
    ground = np.array([0.10, 0.22, 0.07]) * (0.6 + 0.4 * smooth(-1, 0, y))
    c = np.where(y > 0, sky, ground)
    # horizonte brillante
    c = c + np.array([1, 1, 0.9]) * np.exp(-(y / 0.07) ** 2) * 0.18
    # softbox superior izquierdo
    sb = smooth(0.30, 0.18, np.abs(x + 0.45)) * smooth(0.30, 0.18, np.abs(y - 0.60)) * (z > 0)
    c = c + np.array([1.6, 1.6, 1.5]) * sb
    # tira de luz derecha
    st = smooth(0.12, 0.05, np.abs(x - 0.85)) * smooth(0.8, 0.3, np.abs(y)) * (z > -0.2)
    c = c + np.array([0.6, 0.9, 1.1]) * st
    return c


L1 = norm(np.array([-0.55, 0.70, 0.55]))      # luz principal
L2 = norm(np.array([0.75, 0.15, 0.35]))       # relleno frio
LB = norm(np.array([0.0, -1.0, 0.25]))        # rebote del suelo


def shade(Nn, base, rough, spec_k, metal=0.0):
    ndl = np.clip(dot(Nn, L1), 0, 1)
    wrap = np.clip((dot(Nn, L1) + 0.5) / 1.5, 0, 1)
    # subsuperficie falsa: el verde se satura en la zona de transicion
    sss = np.exp(-((dot(Nn, L1) - 0.05) / 0.35) ** 2) * np.array([0.05, 0.22, 0.0])
    diff = base * (0.18 + 1.05 * wrap ** 1.4) * np.array([1.0, 1.0, 0.95])
    diff += base * np.clip(dot(Nn, L2), 0, 1) * np.array([0.10, 0.18, 0.28])
    diff += base * np.clip(dot(Nn, LB), 0, 1) * np.array([0.05, 0.22, 0.04]) * 1.2
    diff += sss
    # especular
    H1 = norm(L1 + V)
    sp = np.clip(dot(Nn, H1), 0, 1) ** (spec_k) * (spec_k + 8) / 40
    H2 = norm(L2 + V)
    sp2 = np.clip(dot(Nn, H2), 0, 1) ** (spec_k * 0.5) * 0.25
    # fresnel + reflejo de entorno
    F0 = 0.04 + 0.6 * metal
    ndv = np.clip(dot(Nn, V), 0, 1)
    fres = F0 + (1 - F0) * (1 - ndv) ** 5
    Rv = norm(2 * dot(Nn, V) * Nn - V)
    refl = env(Rv) * (fres * (1 - rough) * 1.3 + 0.05)
    return diff + (sp + sp2) * np.array([1, 1, 1.0]) * (1 - rough * 0.5) + refl


green = np.array([0.42, 0.78, 0.20])
col_sphere = shade(N, green, 0.18, 90)

# borde: oscurece y satura el verde en el limbo (como el aro oscuro del original)
limb = smooth(0.80, 1.0, np.sqrt(rr2))[..., None]
col_sphere = col_sphere * (1 - 0.55 * limb) + np.array([0.02, 0.12, 0.02]) * 0.55 * limb
# luz que viene de dentro en la parte baja (translucidez)
glow = smooth(-0.2, -0.85, py)[..., None] * smooth(0.0, 0.6, pz)[..., None]
col_sphere += np.array([0.30, 0.62, 0.10]) * glow * 0.18

# oclusion ambiente bajo el texto
ao = 1 - 0.55 * sh_t[..., None]
col_sphere = col_sphere * ao

# texto: negro brillante, bisel con reflejos
black = np.array([0.012, 0.015, 0.012])
col_text = shade(Nt, black, 0.12, 140, metal=0.15)
col = col_sphere * (1 - tm) + col_text * tm

# ---------- aro negro alrededor (como el original) ----------
ring_in, ring_out = 1.0, 1.0 + 0.055
rad = np.sqrt(rr2)
ring = (rad >= ring_in) & (rad < ring_out)
# normal del aro (toro): inclina hacia fuera
t = np.clip((rad - ring_in) / (ring_out - ring_in), 0, 1)
ang = (t - 0.5) * np.pi
nr = np.sin(ang)
dirxy = np.stack([px, py], -1) / np.maximum(rad[..., None], 1e-9)
Nr = norm(np.concatenate([dirxy * nr[..., None], np.cos(ang)[..., None]], -1))
col_ring = shade(Nr, np.array([0.015, 0.02, 0.015]), 0.2, 110, metal=0.2)
ring_aa = np.clip((ring_out - rad) * R * 0.5, 0, 1) * np.clip((rad - (ring_in - 0.004)) * R * 0.5, 0, 1)
disk_alpha = np.clip(aa, 0, 1)
disk_col = col

alpha_obj = np.maximum(disk_alpha, ring_aa)
col_final = np.where(ring[..., None], col_ring, disk_col)
col_final = np.where(ring[..., None], col_final, disk_col)
# transicion esfera->aro
a_disk = (rad < ring_in).astype(float)[..., None]
col_final = disk_col * a_disk + col_ring * (1 - a_disk)
alpha_obj = np.where(rad < ring_in, 1.0, ring_aa)

# ---------- sombra de contacto sobre fondo transparente ----------
sy_ = (ys - (CY + R * 1.17)) / (R * 0.20)
sx_ = (xs - CX) / (R * 0.85)
sh = np.exp(-(sx_ ** 2 + sy_ ** 2) * 1.2)
sh = blur(sh, 0.04 * R) * 0.55
sh = np.clip(sh, 0, 0.6)

# ---------- composicion RGBA (alfa recto) ----------
a_o = alpha_obj
rgb_o = np.clip(col_final, 0, 4)
# tonemap suave
rgb_o = 1 - np.exp(-rgb_o * 1.25)
rgb_o = np.clip(rgb_o, 0, 1) ** (1 / 1.08)
a_s = sh * (1 - a_o)
a_tot = a_o + a_s
rgb = np.where(a_tot[..., None] > 1e-5,
               (rgb_o * a_o[..., None] + np.array([0.0, 0.04, 0.0]) * a_s[..., None]) / np.maximum(a_tot[..., None], 1e-5),
               0.0)
out = np.dstack([np.clip(rgb, 0, 1), np.clip(a_tot, 0, 1)])
im = Image.fromarray((out * 255 + 0.5).astype(np.uint8), "RGBA").resize((1000, 1000), Image.LANCZOS)
im.save(OUT)
print("ok", OUT)
