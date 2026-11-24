"""Genera escenas .iron de logos 3D (texto extruido con bisel + insignia) para probar Iron3D Studio.

Uso: python gen_logos.py <carpeta_salida> [nombre_logo ...]
Cada logo se escribe como <salida>/<nombre>.iron. Se renderiza luego con
Iron3DStudio.exe <escena>.iron --ui-cmd=render-save=W,H,yaw,pitch,dist,tx,ty,tz,<png>

Unidad de Iron3D: 1 unidad = 200 cm. Los logos caben en ~2 unidades.
"""
import math
import os
import sys

import numpy as np
import mapbox_earcut as earcut
from fontTools.ttLib import TTFont
from fontTools.pens.basePen import BasePen

FONTS = {
    "black": "C:/Windows/Fonts/ariblk.ttf",
    "bold": "C:/Windows/Fonts/arialbd.ttf",
    "impact": "C:/Windows/Fonts/impact.ttf",
    "bahn": "C:/Windows/Fonts/bahnschrift.ttf",
}


# ------------------------------------------------------------------ malla
class Mesh:
    def __init__(self):
        self.v = []  # (px,py,pz,nx,ny,nz)
        self.i = []

    def add(self, p, n):
        self.v.append((p[0], p[1], p[2], n[0], n[1], n[2]))
        return len(self.v) - 1

    def tri(self, a, b, c):
        self.i += [a, b, c]

    def quad(self, a, b, c, d):
        self.tri(a, b, c)
        self.tri(a, c, d)

    def merge(self, o, offset=(0, 0, 0)):
        base = len(self.v)
        for x in o.v:
            self.v.append((x[0] + offset[0], x[1] + offset[1], x[2] + offset[2], x[3], x[4], x[5]))
        self.i += [k + base for k in o.i]


def norm(v):
    l = math.sqrt(sum(c * c for c in v)) or 1.0
    return tuple(c / l for c in v)


def sphere(r, seg=64, rings=32):
    m = Mesh()
    for j in range(rings + 1):
        th = math.pi * j / rings
        for k in range(seg + 1):
            ph = 2 * math.pi * k / seg
            n = (math.sin(th) * math.sin(ph), math.cos(th), math.sin(th) * math.cos(ph))
            m.add((n[0] * r, n[1] * r, n[2] * r), n)
    for j in range(rings):
        for k in range(seg):
            a = j * (seg + 1) + k
            b = a + seg + 1
            m.quad(a, a + 1, b + 1, b)
    return m


def torus(R, r, seg=96, ring=24):
    """Anillo en el plano XY (eje Z)."""
    m = Mesh()
    for k in range(seg + 1):
        ph = 2 * math.pi * k / seg
        cx, cy = math.cos(ph), math.sin(ph)
        for j in range(ring + 1):
            th = 2 * math.pi * j / ring
            n = (math.cos(th) * cx, math.cos(th) * cy, math.sin(th))
            m.add(((R + r * math.cos(th)) * cx, (R + r * math.cos(th)) * cy, r * math.sin(th)), n)
    for k in range(seg):
        for j in range(ring):
            a = k * (ring + 1) + j
            b = a + ring + 1
            m.quad(a, b, b + 1, a + 1)
    return m


def disc(r, depth, seg=64, bevel=0.0, bsteps=6):
    """Cilindro-moneda en el plano XY, eje Z, centrado; borde con bisel redondeado."""
    prof = []  # (radio, z, nr, nz)
    h = depth / 2
    b = min(bevel, h, r * 0.5)
    prof.append((0.0, h, 0.0, 1.0))
    if b > 0:
        for s in range(bsteps + 1):
            t = (math.pi / 2) * s / bsteps
            prof.append((r - b + b * math.sin(t), h - b + b * math.cos(t), math.sin(t), math.cos(t)))
        for s in range(bsteps + 1):
            t = (math.pi / 2) * s / bsteps
            prof.append((r - b + b * math.cos(t), -h + b - b * math.sin(t), math.cos(t), -math.sin(t)))
    else:
        prof += [(r, h, 0.0, 1.0), (r, -h, 0.0, -1.0)]
    prof.append((0.0, -h, 0.0, -1.0))
    m = Mesh()
    for (rad, z, nr, nz) in prof:
        for k in range(seg + 1):
            ph = 2 * math.pi * k / seg
            c, s = math.cos(ph), math.sin(ph)
            m.add((rad * c, rad * s, z), (nr * c, nr * s, nz))
    for j in range(len(prof) - 1):
        for k in range(seg):
            a = j * (seg + 1) + k
            b2 = a + seg + 1
            m.quad(a, b2, b2 + 1, a + 1)
    return m


def prism(r, depth, sides=6, bevel=0.0):
    """Prisma regular (hexagono por defecto) en el plano XY con las caras planas."""
    m = Mesh()
    h = depth / 2
    pts = [(r * math.cos(2 * math.pi * k / sides + math.pi / sides), r * math.sin(2 * math.pi * k / sides + math.pi / sides)) for k in range(sides)]
    for z, nz in ((h, 1.0), (-h, -1.0)):
        c = m.add((0, 0, z), (0, 0, nz))
        ring = [m.add((p[0], p[1], z), (0, 0, nz)) for p in pts]
        for k in range(sides):
            if nz > 0:
                m.tri(c, ring[k], ring[(k + 1) % sides])
            else:
                m.tri(c, ring[(k + 1) % sides], ring[k])
    for k in range(sides):
        p0, p1 = pts[k], pts[(k + 1) % sides]
        n = norm(((p0[1] + p1[1]) / 2, -(p0[0] + p1[0]) / 2 * 0 + 0, 0))
        mx, my = (p0[0] + p1[0]) / 2, (p0[1] + p1[1]) / 2
        n = norm((mx, my, 0))
        a = m.add((p0[0], p0[1], h), n)
        b = m.add((p1[0], p1[1], h), n)
        c = m.add((p1[0], p1[1], -h), n)
        d = m.add((p0[0], p0[1], -h), n)
        m.quad(a, d, c, b)
    return m


def box(w, h, d):
    m = Mesh()
    x, y, z = w / 2, h / 2, d / 2
    faces = [((1, 0, 0), [(x, -y, -z), (x, y, -z), (x, y, z), (x, -y, z)]),
             ((-1, 0, 0), [(-x, -y, z), (-x, y, z), (-x, y, -z), (-x, -y, -z)]),
             ((0, 1, 0), [(-x, y, -z), (-x, y, z), (x, y, z), (x, y, -z)]),
             ((0, -1, 0), [(-x, -y, z), (-x, -y, -z), (x, -y, -z), (x, -y, z)]),
             ((0, 0, 1), [(-x, -y, z), (x, -y, z), (x, y, z), (-x, y, z)]),
             ((0, 0, -1), [(x, -y, -z), (-x, -y, -z), (-x, y, -z), (x, y, -z)])]
    for n, ps in faces:
        ids = [m.add(p, n) for p in ps]
        m.quad(*ids)
    return m


# ------------------------------------------------------------------ texto
class _Pen(BasePen):
    def __init__(self, gs):
        super().__init__(gs)
        self.contours = []
        self.cur = None

    def _moveTo(self, p):
        self.cur = [p]

    def _lineTo(self, p):
        self.cur.append(p)

    def _curveToOne(self, p1, p2, p3):
        p0 = self.cur[-1]
        for s in range(1, 11):
            t = s / 10
            mt = 1 - t
            self.cur.append((mt ** 3 * p0[0] + 3 * mt * mt * t * p1[0] + 3 * mt * t * t * p2[0] + t ** 3 * p3[0],
                             mt ** 3 * p0[1] + 3 * mt * mt * t * p1[1] + 3 * mt * t * t * p2[1] + t ** 3 * p3[1]))

    def _qCurveToOne(self, p1, p2):
        p0 = self.cur[-1]
        for s in range(1, 9):
            t = s / 8
            mt = 1 - t
            self.cur.append((mt * mt * p0[0] + 2 * mt * t * p1[0] + t * t * p2[0],
                             mt * mt * p0[1] + 2 * mt * t * p1[1] + t * t * p2[1]))

    def _closePath(self):
        if self.cur and len(self.cur) > 2:
            self.contours.append(self.cur)
        self.cur = None

    _endPath = _closePath


def area2(c):
    return sum(c[i][0] * c[(i + 1) % len(c)][1] - c[(i + 1) % len(c)][0] * c[i][1] for i in range(len(c))) / 2


def clean(c, eps=1e-4):
    out = []
    for p in c:
        if not out or math.hypot(p[0] - out[-1][0], p[1] - out[-1][1]) > eps:
            out.append(p)
    while len(out) > 1 and math.hypot(out[0][0] - out[-1][0], out[0][1] - out[-1][1]) <= eps:
        out.pop()
    return out


def inside(pt, poly):
    x, y = pt
    c = False
    n = len(poly)
    for i in range(n):
        x1, y1 = poly[i]
        x2, y2 = poly[(i + 1) % n]
        if (y1 > y) != (y2 > y) and x < (x2 - x1) * (y - y1) / (y2 - y1) + x1:
            c = not c
    return c


def extrude_shape(outer, holes, depth, bevel, bsteps=4):
    """Contorno exterior CCW + huecos CW (y arriba) extruido con bisel redondeado."""
    m = Mesh()
    rings = [outer] + holes
    h = depth / 2
    b = min(bevel, h * 0.95)
    # perfil de bisel: (inset, z, factor_normal_horizontal, nz)
    prof = []
    for s in range(bsteps + 1):
        t = (math.pi / 2) * s / bsteps
        prof.append((b * (1 - math.sin(t)), h - b * (1 - math.cos(t)), math.sin(t), math.cos(t)))
    prof_front = prof
    prof_back = [(i, -z, hn, -nz) for (i, z, hn, nz) in reversed(prof)]
    profile = prof_front + prof_back  # de la tapa frontal a la trasera

    def inset_ring(ring, amount):
        n = len(ring)
        res = []
        for i in range(n):
            p0, p1, p2 = ring[i - 1], ring[i], ring[(i + 1) % n]
            e1 = norm((p1[0] - p0[0], p1[1] - p0[1], 0))
            e2 = norm((p2[0] - p1[0], p2[1] - p1[1], 0))
            # material a la izquierda en CCW exterior: dentro = normal izquierda (-dy, dx)
            n1 = (-e1[1], e1[0])
            n2 = (-e2[1], e2[0])
            mx, my = n1[0] + n2[0], n1[1] + n2[1]
            ml = math.hypot(mx, my) or 1.0
            mx, my = mx / ml, my / ml
            cosh = max(0.7, mx * n1[0] + my * n1[1])
            res.append((p1[0] + mx * amount / cosh, p1[1] + my * amount / cosh))
        return res

    # tapas: contorno desplazado b hacia dentro (para que el bisel no se salga)
    cap_rings = [inset_ring(r, prof[0][0]) for r in rings]
    flat = np.array([p for r in cap_rings for p in r], dtype=np.float32)
    ends = np.cumsum([len(r) for r in cap_rings]).astype(np.uint32)
    tris = earcut.triangulate_float32(flat, ends).reshape(-1, 3)
    for z, nz in ((h, 1.0), (-h, -1.0)):
        base = len(m.v)
        for p in flat:
            m.add((float(p[0]), float(p[1]), z), (0, 0, nz))
        for a, b2, c in tris:
            if nz > 0:
                m.tri(base + int(a), base + int(b2), base + int(c))
            else:
                m.tri(base + int(a), base + int(c), base + int(b2))
    # lados + biseles
    for ring in rings:
        n = len(ring)
        # normales exteriores por arista (derecha de la direccion en CCW exterior)
        edge_n = []
        for i in range(n):
            p1, p2 = ring[i], ring[(i + 1) % n]
            e = norm((p2[0] - p1[0], p2[1] - p1[1], 0))
            edge_n.append((e[1], -e[0]))
        vtx_n = []
        for i in range(n):
            a, c = edge_n[i - 1], edge_n[i]
            dot = a[0] * c[0] + a[1] * c[1]
            if dot > 0.80:  # esquina suave
                nn = norm((a[0] + c[0], a[1] + c[1], 0))
                vtx_n.append(((nn[0], nn[1]), (nn[0], nn[1])))
            else:  # esquina viva: cada arista conserva la suya
                vtx_n.append((a, c))
        layers = []
        for (ins, z, hn, nz) in profile:
            pos = inset_ring(ring, ins)
            layers.append((pos, z, hn, nz))
        for i in range(n):
            j = (i + 1) % n
            ids = []
            for (pos, z, hn, nz) in layers:
                # vertices de esta arista: inicio usa la normal "saliente" del vertice i, fin la "entrante" del j
                ni = vtx_n[i][1]
                nj = vtx_n[j][0]
                ids.append((m.add((pos[i][0], pos[i][1], z), (ni[0] * hn, ni[1] * hn, nz)),
                            m.add((pos[j][0], pos[j][1], z), (nj[0] * hn, nj[1] * hn, nz))))
            for k in range(len(ids) - 1):
                a0, a1 = ids[k]
                b0, b1 = ids[k + 1]
                m.quad(a0, b0, b1, a1)
    return m


def text_mesh(text, font="black", size=1.0, depth=0.3, bevel=0.022, tracking=0.0):
    f = TTFont(FONTS[font])
    gs = f.getGlyphSet()
    cmap = f.getBestCmap()
    upm = f["head"].unitsPerEm
    sc = size / upm
    total = Mesh()
    x = 0.0
    for ch in text:
        gname = cmap.get(ord(ch))
        if gname is None:
            continue
        pen = _Pen(gs)
        gs[gname].draw(pen)
        conts = [clean([(p[0] * sc + x, p[1] * sc) for p in c]) for c in pen.contours]
        conts = [c for c in conts if len(c) > 2]
        # TrueType: exteriores horarios (area<0). Se normalizan: exterior CCW, hueco CW.
        outers, holes = [], []
        for c in conts:
            a = area2(c)
            (outers if a < 0 else holes).append(list(reversed(c)) if a < 0 else list(reversed(c)))
        # tras invertir: exteriores CCW (area>0); huecos CW
        for o in outers:
            hs = [hh for hh in holes if inside(hh[0], o)]
            total.merge(extrude_shape(o, hs, depth, bevel))
        x += gs[gname].width * sc + tracking * size
    # centrar en X/Y (caja de contorno)
    xs = [v[0] for v in total.v]
    ys = [v[1] for v in total.v]
    cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
    total.v = [(v[0] - cx, v[1] - cy, v[2], v[3], v[4], v[5]) for v in total.v]
    return total, (max(xs) - min(xs), max(ys) - min(ys))


# ------------------------------------------------------------------ escena
class Scene:
    def __init__(self, bg_top=(1, 1, 1), bg_bottom=(0.82, 0.84, 0.86)):
        self.mats = []
        self.nodes = []
        self.bg = (bg_top, bg_bottom)

    def mat(self, name, color, metallic=0.0, roughness=0.5, emission=(0, 0, 0), transparency=0.0):
        self.mats.append((name, color, metallic, roughness, emission, transparency))
        return len(self.mats) - 1

    def add(self, name, mesh, material, pos=(0, 0, 0), rot=(0, 0, 0), scale=(1, 1, 1)):
        self.nodes.append((name, mesh, material, pos, rot, scale))

    def write(self, path):
        o = []
        o.append("IRON3D_SCENE 9")
        o.append("MATERIALS {")
        for (n, c, me, ro, em, tr) in self.mats:
            o.append('  MATERIAL name="%s" br=%.5f bg=%.5f bb=%.5f metallic=%.4f roughness=%.4f er=%.4f eg=%.4f eb=%.4f tr=%.4f tg=%.4f tb=%.4f ut=%d {' % ((n,) + tuple(c) + (me, ro) + tuple(em) + (tr, tr, tr, 1 if tr > 0 else 0)))
            o.append("  }")
        o.append("}")
        o.append('NODE name="Escena" primitive=none px=0 py=0 pz=0 rx=0 ry=0 rz=0 sx=1 sy=1 sz=1 editorVis=0 renderVis=0 materialIndex=-1 {')
        o.append('  NODE name="Fondo" primitive=none px=0 py=0 pz=0 rx=0 ry=0 rz=0 sx=1 sy=1 sz=1 editorVis=0 renderVis=0 materialIndex=-1 {')
        o.append("  ENV kind=0 tr=%f tg=%f tb=%f br=%f bg=%f bb=%f" % (tuple(self.bg[0]) + tuple(self.bg[1])))
        o.append("  }")
        for (name, mesh, mi, pos, rot, sc) in self.nodes:
            o.append('  NODE name="%s" primitive=mesh px=%f py=%f pz=%f rx=%f ry=%f rz=%f sx=%f sy=%f sz=%f editorVis=0 renderVis=0 materialIndex=%d {' % ((name,) + tuple(pos) + tuple(rot) + tuple(sc) + (mi,)))
            o.append("    MESH nv=%d ni=%d {" % (len(mesh.v), len(mesh.i)))
            for v in mesh.v:
                o.append("      %.6f %.6f %.6f %.5f %.5f %.5f" % v)
            for k in range(0, len(mesh.i), 3):
                o.append("      %d %d %d" % tuple(mesh.i[k:k + 3]))
            o.append("    }")
            o.append("  }")
        o.append("}")
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(o) + "\n")


# ------------------------------------------------------------------ logos
def logo_i3ds(out):
    """Calibracion: el logo I3DS del propio Rooty (esfera de cristal verde, letras oscuras)."""
    s = Scene()
    ball = s.mat("Esfera", (0.22, 0.60, 0.14), 0.0, 0.12)
    rim = s.mat("Aro", (0.05, 0.10, 0.06), 0.55, 0.22)
    ink = s.mat("Letras", (0.03, 0.09, 0.06), 0.35, 0.10)
    s.add("Esfera", sphere(1.0), ball, scale=(1, 1, 0.42))
    s.add("Aro", torus(0.99, 0.035), rim, pos=(0, 0, 0))
    t, (w, h) = text_mesh("I3DS", "black", 0.62, 0.30, 0.025)
    k = 1.25 / w
    s.add("Texto", t, ink, pos=(0, -0.02, 0.55), scale=(k, k, 1))
    s.write(out)


def badge(out, text, face, rim, ink, bg=None, shape="disc", font="black", glow=None, ink_metal=0.2, face_metal=0.0, tsize=0.62, twidth=1.25, face_rough=0.3):
    s = Scene(*(bg or ((1, 1, 1), (0.82, 0.84, 0.86))))
    mf = s.mat("Cara", face, face_metal, face_rough)
    mr = s.mat("Borde", rim, 0.7, 0.2)
    mi = s.mat("Letras", ink, ink_metal, 0.25, glow or (0, 0, 0))
    if shape == "hex":
        s.add("Placa", prism(1.05, 0.22), mr)
        s.add("Cara", prism(0.93, 0.26), mf)
    elif shape == "sphere":
        s.add("Esfera", sphere(1.0), mf, scale=(1, 1, 0.42))
        s.add("Aro", torus(0.99, 0.035), mr)
    else:
        s.add("Moneda", disc(1.0, 0.30, 96, 0.08), mr)
        s.add("Cara", disc(0.88, 0.34, 96, 0.04), mf)
    t, (w, h) = text_mesh(text, font, tsize, 0.28, 0.022)
    k = twidth / w
    s.add("Texto", t, mi, pos=(0, 0, 0.30), scale=(k, k, 1))
    s.write(out)


def roundrect(w, h, r, seg=14):
    """Rectangulo de esquinas redondas, CCW (y arriba), centrado en el origen."""
    pts = []
    corners = [(w / 2 - r, h / 2 - r, 0), (-w / 2 + r, h / 2 - r, 90), (-w / 2 + r, -h / 2 + r, 180), (w / 2 - r, -h / 2 + r, 270)]
    for cx, cy, a0 in corners:
        for s in range(seg + 1):
            a = math.radians(a0 + 90 * s / seg)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def faceted_sphere(r, seg=10, rings=6):
    """Esfera de baja resolucion con sombreado plano (gema)."""
    base = sphere(r, seg, rings)
    m = Mesh()
    for k in range(0, len(base.i), 3):
        a, b, c = (base.v[j] for j in base.i[k:k + 3])
        e1 = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
        e2 = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
        n = norm((e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]))
        if sum(n[i] * (a[i] + b[i] + c[i]) for i in range(3)) < 0:
            n = (-n[0], -n[1], -n[2])
        if (e1[0] ** 2 + e1[1] ** 2 + e1[2] ** 2) < 1e-12 or (e2[0] ** 2 + e2[1] ** 2 + e2[2] ** 2) < 1e-12:
            continue
        ids = [m.add((v[0], v[1], v[2]), n) for v in (a, b, c)]
        m.tri(*ids)
    return m


def glass(out, text, shape, tint, opacity=0.78, ink=(0.03, 0.08, 0.05), ink_metal=0.35, glow=None, ring=(0.05, 0.10, 0.06),
          ring_metal=0.6, font="black", tsize=0.62, twidth=1.25, rough=0.06, ink_rough=0.12):
    """Logo de cristal tintado con letras dentro, como el I3DS de Rooty."""
    s = Scene()
    # El render por CPU mezcla en espacio de pantalla: el tinte se usa tal cual y el cristal es ligero
    # (opacidad efectiva 55 % de la pedida) para que las letras de dentro se vean.
    mg = s.mat("Cristal", tint, 0.0, rough, transparency=1.0 - opacity * 0.55)
    mr = s.mat("Marco", ring, ring_metal, 0.18)
    mi = s.mat("Letras", ink, ink_metal, ink_rough, glow or (0, 0, 0))
    tz = 0.02
    if shape == "sphere":
        s.add("Cristal", sphere(1.0), mg, scale=(1, 1, 0.55))
        s.add("Aro", torus(0.99, 0.035), mr)
        tz = 0.2
    elif shape == "disc":
        s.add("Marco", disc(1.04, 0.14, 96, 0.05), mr, pos=(0, 0, -0.1))
        s.add("Cristal", disc(1.0, 0.5, 96, 0.2), mg)
    elif shape == "hex":
        s.add("Marco", prism(1.12, 0.14), mr, pos=(0, 0, -0.1))
        s.add("Cristal", prism(1.0, 0.5), mg)
    elif shape == "roundrect":
        s.add("Marco", extrude_shape(roundrect(1.95, 1.95, 0.42), [], 0.14, 0.04), mr, pos=(0, 0, -0.1))
        s.add("Cristal", extrude_shape(roundrect(1.85, 1.85, 0.40), [], 0.5, 0.2), mg)
    elif shape == "pill":
        s.add("Marco", extrude_shape(roundrect(2.2, 1.1, 0.55), [], 0.14, 0.04), mr, pos=(0, 0, -0.1))
        s.add("Cristal", extrude_shape(roundrect(2.1, 1.0, 0.5), [], 0.5, 0.2), mg)
    elif shape == "gem":
        s.add("Cristal", faceted_sphere(1.0), mg, scale=(1, 1, 0.6))
        tz = 0.2
    t, (w, h) = text_mesh(text, font, tsize, 0.2, 0.02)
    k = twidth / w
    s.add("Texto", t, mi, pos=(0, 0, tz), scale=(k, k, 1))
    s.write(out)


GLASS = {
    "g01_i3ds_esfera_verde": lambda p: glass(p, "I3DS", "sphere", (0.28, 0.68, 0.16), 0.80),
    "g02_sf_hex_ambar": lambda p: glass(p, "SF", "hex", (0.95, 0.55, 0.08), 0.78, ink=(0.10, 0.04, 0.02), ring=(0.18, 0.10, 0.04), twidth=1.1),
    "g03_core_pastilla_cian": lambda p: glass(p, "CORE", "pill", (0.12, 0.70, 0.90), 0.78, ink=(0.02, 0.07, 0.12), ring=(0.06, 0.12, 0.18), font="bahn", tsize=0.5, twidth=1.75),
    "g04_kc_icono_azul": lambda p: glass(p, "KC", "roundrect", (0.12, 0.35, 0.92), 0.80, ink=(0.93, 0.97, 1.0), ink_metal=0.1, ring=(0.75, 0.82, 0.95), ring_metal=0.9, twidth=1.15),
    "g05_rck_moneda_esmeralda": lambda p: glass(p, "RCK", "disc", (0.10, 0.75, 0.50), 0.78, ink=(0.02, 0.10, 0.07), ring=(0.07, 0.16, 0.12), font="bold", twidth=1.3),
    "g06_fos_esfera_roja_neon": lambda p: glass(p, "FOS", "sphere", (0.85, 0.12, 0.08), 0.82, ink=(1.0, 0.65, 0.2), ink_metal=0.0, glow=(0.9, 0.45, 0.08), ring=(0.2, 0.05, 0.03), font="impact", twidth=1.3),
    "g07_xusb_pastilla_amarilla": lambda p: glass(p, "X-USB", "pill", (0.98, 0.82, 0.10), 0.78, ink=(0.12, 0.09, 0.02), ring=(0.25, 0.20, 0.05), font="impact", tsize=0.55, twidth=1.7),
    "g08_sl_esfera_humo_cromo": lambda p: glass(p, "SL", "sphere", (0.10, 0.12, 0.16), 0.88, ink=(0.85, 0.88, 0.92), ink_metal=1.0, ink_rough=0.08, ring=(0.8, 0.82, 0.86), ring_metal=1.0, font="bold", twidth=1.1),
    "g09_wf_hex_violeta": lambda p: glass(p, "WF", "hex", (0.55, 0.22, 0.90), 0.78, ink=(0.95, 0.85, 1.0), ink_metal=0.2, ring=(0.12, 0.06, 0.2), twidth=1.2),
    "g10_cm_gema_carbon": lambda p: glass(p, "CM", "gem", (0.06, 0.06, 0.08), 0.86, ink=(1.0, 0.55, 0.10), ink_metal=0.0, glow=(1.0, 0.40, 0.05), ring=(0.1, 0.1, 0.12), twidth=1.2),
}

LOGOS = {
    "i3ds": lambda p: logo_i3ds(p),
    "staticforge": lambda p: badge(p, "SF", (0.10, 0.11, 0.13), (0.75, 0.40, 0.10), (1.0, 0.47, 0.08), shape="hex", glow=(0.25, 0.10, 0.0), ink_metal=0.6),
    "core": lambda p: badge(p, "CORE", (0.05, 0.06, 0.10), (0.15, 0.80, 0.95), (0.35, 0.95, 1.0), glow=(0.0, 0.15, 0.2), font="bahn", tsize=0.5, twidth=1.4),
    "kcdownloader": lambda p: badge(p, "KC", (0.10, 0.28, 0.65), (0.82, 0.88, 0.97), (0.96, 0.98, 1.0), shape="sphere", face_rough=0.12, ink_metal=0.1),
    "rck": lambda p: badge(p, "RCK", (0.08, 0.09, 0.12), (0.30, 0.85, 0.65), (0.45, 1.0, 0.75), shape="hex", glow=(0.0, 0.18, 0.1), font="bold", twidth=1.3),
    "forgeos": lambda p: badge(p, "FOS", (0.14, 0.09, 0.07), (0.85, 0.25, 0.08), (1.0, 0.55, 0.15), glow=(0.3, 0.08, 0.0), font="impact", twidth=1.35),
    "xusb": lambda p: badge(p, "X-USB", (0.10, 0.10, 0.12), (0.90, 0.85, 0.15), (1.0, 0.95, 0.30), shape="hex", font="impact", twidth=1.45, tsize=0.55),
    "staticlinux": lambda p: badge(p, "SL", (0.07, 0.12, 0.20), (0.60, 0.70, 0.85), (0.80, 0.88, 1.0), shape="sphere", font="bold", ink_metal=0.5),
    "winforge": lambda p: badge(p, "WF", (0.05, 0.30, 0.65), (0.15, 0.15, 0.18), (0.95, 0.60, 0.15), glow=(0.2, 0.10, 0.0), font="black"),
    "coalmix": lambda p: badge(p, "CM", (0.04, 0.04, 0.05), (0.50, 0.50, 0.55), (1.0, 0.45, 0.05), shape="hex", glow=(0.30, 0.10, 0.0), font="black", face_metal=0.4),
}

if __name__ == "__main__":
    outdir = sys.argv[1]
    os.makedirs(outdir, exist_ok=True)
    table = dict(LOGOS)
    table.update(GLASS)
    names = sys.argv[2:] or list(LOGOS)
    if names == ["glass"]:
        names = list(GLASS)
    for n in names:
        table[n](os.path.join(outdir, n + ".iron"))
        print("ok", n)
