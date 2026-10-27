"""CLI: comprueba tu carpeta de plugins de R19 sin abrir Iron3D.

    python -m iron3d_py2compat scan <carpeta_plugins>
    python -m iron3d_py2compat translate <archivo.pyp>

'scan' solo traduce (no ejecuta nada): dice que plugins convierten bien,
cuantas divisiones Py2 se han reescrito y cuales estan cifrados.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

from .translator import Py2Translator, TranslationError


def cmd_scan(folder: Path) -> int:
    translator = Py2Translator(cache_dir=None)
    files = sorted(
        p for p in folder.rglob("*")
        if p.is_file() and p.suffix.lower() in (".pyp", ".pypv", ".py")
    )
    if not files:
        print(f"No hay .pyp / .py en {folder}")
        return 0

    ok = errors = encrypted = 0
    for path in files:
        rel = path.relative_to(folder)
        if path.suffix.lower() == ".pypv":
            encrypted += 1
            print(f"  CIFRADO   {rel}  (necesita el .pyp del autor)")
            continue
        try:
            result = translator.translate_file(path)
        except TranslationError as e:
            errors += 1
            print(f"  ERROR     {rel}  -> {e}")
            continue
        ok += 1
        notes = [f"{result.old_div_count} divisiones Py2"] if result.old_div_count else []
        if result.tabs_normalized:
            notes.append("tabs normalizados")
        extra = f" ({', '.join(notes)})" if notes else ""
        print(f"  OK        {rel}{extra}")

    print(f"\n{ok} OK · {errors} con error · {encrypted} cifrados · {len(files)} archivos")
    return 1 if errors else 0


def cmd_translate(path: Path) -> int:
    try:
        result = Py2Translator(cache_dir=None).translate_file(path)
    except TranslationError as e:
        print(e, file=sys.stderr)
        return 1
    sys.stdout.write(result.source)
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="iron3d_py2compat")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("scan", help="analiza una carpeta de plugins").add_argument("folder", type=Path)
    sub.add_parser("translate", help="imprime un plugin traducido").add_argument("file", type=Path)
    args = parser.parse_args(argv)

    if args.command == "scan":
        return cmd_scan(args.folder)
    return cmd_translate(args.file)


if __name__ == "__main__":
    sys.exit(main())
