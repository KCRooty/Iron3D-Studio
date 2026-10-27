"""Builtins de Python 2 para el espacio de nombres de cada plugin legacy.

Se inyectan como __builtins__ del modulo del plugin, nunca en el builtins
global: un plugin no puede cambiar lo que ve Iron3D ni lo que ven otros plugins.
"""
from __future__ import annotations

import builtins
import functools
import io
import numbers
import sys
from typing import Any


def old_div(a: Any, b: Any) -> Any:
    """Division con semantica de Python 2.

    int / int -> division entera redondeando hacia -inf (7/2 == 3, -7/2 == -4).
    Cualquier otra cosa (floats, Vector de c4d-compat...) -> division real.
    """
    if isinstance(a, numbers.Integral) and isinstance(b, numbers.Integral):
        return a // b
    return a / b


def cmp(a: Any, b: Any) -> int:
    return (a > b) - (a < b)


def apply(func, args=(), kwargs=None):
    return func(*args, **(kwargs or {}))


def py2_open(file, mode="r", buffering=-1, *args, **kwargs):
    """open() con el comportamiento practico de Py2.

    En Py2, open(f) sin 'b' devolvia bytes como str. Aqui el modo texto usa
    UTF-8 con 'surrogateescape' y newline="" (sin traducir saltos de linea):
    un texto normal se lee bien y unos bytes arbitrarios sobreviven intactos
    a un ciclo leer -> escribir, igual en Windows y en Linux.
    Los modos binarios ('rb', 'wb'...) pasan tal cual.
    """
    if "b" in mode:
        return io.open(file, mode, buffering, *args, **kwargs)
    kwargs.setdefault("encoding", "utf-8")
    kwargs.setdefault("errors", "surrogateescape")
    kwargs.setdefault("newline", "")
    return io.open(file, mode.replace("U", ""), buffering, *args, **kwargs)


PY2_SHIMS: dict[str, Any] = {
    "unicode": str,
    "long": int,
    "basestring": str,
    "xrange": range,
    "raw_input": input,
    "unichr": chr,
    "intern": sys.intern,
    "buffer": memoryview,
    "reduce": functools.reduce,
    "cmp": cmp,
    "apply": apply,
    "file": py2_open,
    "open": py2_open,
    "old_div": old_div,
}


def make_plugin_builtins(extra: dict[str, Any] | None = None) -> dict[str, Any]:
    """Copia de builtins + shims Py2 (+ extras de c4d-compat, p. ej. __res__)."""
    namespace = dict(vars(builtins))
    namespace.update(PY2_SHIMS)
    if extra:
        namespace.update(extra)
    return namespace
