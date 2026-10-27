# -*- coding: utf-8 -*-
"""
dump_c4d_api.py - Iron3D Studio / c4d-compat
-------------------------------------------------
Vuelca la INTERFAZ publica de la API Python de Cinema 4D R19 a JSON:
modulos, clases, herencia, metodos, propiedades y valores de constantes.

Clean-room: NO guarda docstrings ni codigo, solo lo funcional
(nombres, tipos, jerarquia, IDs) necesario para interoperar.

Uso: Cinema 4D R19 > Script > Script Manager > pegar > Execute.
Python 2.7 (el de R19). Salida: c4d_r19_api.json en el Escritorio.
"""
import c4d
import json
import os
import sys
import time
import inspect

SUBMODULES = [
    "c4d.bitmaps", "c4d.documents", "c4d.gui", "c4d.plugins",
    "c4d.storage", "c4d.threading", "c4d.utils", "c4d.modules",
    "c4d.modules.mograph", "c4d.modules.character", "c4d.modules.hair",
    "c4d.modules.thinkingparticles", "c4d.modules.snap",
    "c4d.modules.bodypaint", "c4d.modules.render", "c4d.modules.sculpting",
    "c4d.modules.graphview", "c4d.modules.net", "c4d.modules.takesystem",
    "c4d.modules.colorchooser", "c4d.modules.volume",
    "c4d.utils.noise",
]

SKIP_DUNDERS = set([
    "__doc__", "__module__", "__dict__", "__weakref__",
    "__slots__", "__class__", "__subclasshook__",
])

PRIMITIVES = (int, long, float, bool, str, unicode)

STATS = {"modules": 0, "classes": 0, "functions": 0,
         "methods": 0, "properties": 0, "constants": 0}

SEEN_CLASSES = {}  # id(cls) -> nombre cualificado


def qualname(cls):
    return "%s.%s" % (getattr(cls, "__module__", "?"), cls.__name__)


def safe_text(val):
    if isinstance(val, unicode):
        return val
    try:
        return val.decode("utf-8")
    except Exception:
        return repr(val)


def describe_value(val):
    """Describe un miembro sin guardar documentacion."""
    if (inspect.isgetsetdescriptor(val) or inspect.ismemberdescriptor(val)
            or isinstance(val, property)):
        STATS["properties"] += 1
        return {"kind": "property"}

    if inspect.isclass(val):
        return {"kind": "class_ref", "target": qualname(val)}

    if inspect.ismodule(val):
        return {"kind": "module_ref", "target": val.__name__}

    if isinstance(val, PRIMITIVES):
        STATS["constants"] += 1
        if isinstance(val, (str, unicode)):
            return {"kind": "constant", "type": "str", "value": safe_text(val)}
        return {"kind": "constant", "type": type(val).__name__, "value": val}

    if inspect.ismethoddescriptor(val) or inspect.ismethod(val):
        STATS["methods"] += 1
        entry = {"kind": "method"}
    elif inspect.isbuiltin(val) or inspect.isfunction(val):
        STATS["functions"] += 1
        entry = {"kind": "function"}
    elif callable(val):
        entry = {"kind": "callable"}
    else:
        return {"kind": "object", "type": type(val).__name__}

    # Firma solo si es Python puro (las de C no la exponen en 2.7)
    try:
        spec = inspect.getargspec(val)
        entry["args"] = spec.args
        entry["varargs"] = spec.varargs
        entry["keywords"] = spec.keywords
    except Exception:
        pass
    return entry


def dump_class(cls):
    key = id(cls)
    if key in SEEN_CLASSES:
        return {"kind": "class_ref", "target": SEEN_CLASSES[key]}
    SEEN_CLASSES[key] = qualname(cls)
    STATS["classes"] += 1

    info = {
        "kind": "class",
        "qualname": qualname(cls),
        "bases": [qualname(b) for b in cls.__bases__],
        "mro": [qualname(b) for b in inspect.getmro(cls)[1:]],
        "members": {},
    }
    # Solo miembros propios: lo heredado se reconstruye con "bases"
    for name in sorted(cls.__dict__.keys()):
        if name in SKIP_DUNDERS:
            continue
        try:
            val = getattr(cls, name)
        except Exception:
            val = cls.__dict__[name]
        try:
            info["members"][name] = describe_value(val)
        except Exception as e:
            info["members"][name] = {"kind": "error", "error": str(e)}
    return info


def dump_module(mod):
    STATS["modules"] += 1
    out = {"kind": "module", "name": mod.__name__, "members": {}}
    for name in sorted(dir(mod)):
        if name.startswith("_"):
            continue
        try:
            val = getattr(mod, name)
        except Exception as e:
            out["members"][name] = {"kind": "error", "error": str(e)}
            continue
        try:
            if inspect.isclass(val):
                out["members"][name] = dump_class(val)
            else:
                out["members"][name] = describe_value(val)
        except Exception as e:
            out["members"][name] = {"kind": "error", "error": str(e)}
    return out


def import_module(name):
    try:
        return __import__(name, fromlist=["*"])
    except Exception:
        return None


def main():
    t0 = time.time()
    result = {
        "meta": {
            "tool": "dump_c4d_api.py (Iron3D Studio c4d-compat)",
            "c4d_version": c4d.GetC4DVersion(),
            "python": sys.version.split()[0],
            "date": time.strftime("%Y-%m-%d %H:%M:%S"),
            "missing_modules": [],
        },
        "modules": {},
    }

    result["modules"]["c4d"] = dump_module(c4d)
    for name in SUBMODULES:
        mod = import_module(name)
        if mod is None:
            result["meta"]["missing_modules"].append(name)
            continue
        result["modules"][name] = dump_module(mod)

    result["meta"]["stats"] = STATS
    result["meta"]["seconds"] = round(time.time() - t0, 2)

    desktop = c4d.storage.GeGetC4DPath(c4d.C4D_PATH_DESKTOP)
    path = os.path.join(desktop, "c4d_r19_api.json")
    with open(path, "wb") as f:
        f.write(json.dumps(result, indent=2, sort_keys=True, ensure_ascii=True))

    print("=" * 50)
    print("API de C4D volcada en: %s" % path)
    for k in sorted(STATS):
        print("  %-11s %d" % (k, STATS[k]))
    if result["meta"]["missing_modules"]:
        print("  No disponibles: %s" % ", ".join(result["meta"]["missing_modules"]))
    print("  Tiempo: %ss" % result["meta"]["seconds"])
    print("=" * 50)


if __name__ == "__main__":
    main()
