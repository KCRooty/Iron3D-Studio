"""Fixers propios de Iron3D para lo que fissix/2to3 no cubre."""
import re

from fissix.pgen2 import token

# "from __future__ import division" -> el archivo ya usa la division de Py3.
FUTURE_DIVISION = re.compile(
    r"^[ \t]*from[ \t]+__future__[ \t]+import[^\n]*\bdivision\b", re.MULTILINE
)


def is_float_literal(node) -> bool:
    """True si el nodo es un literal float/complejo (7.0, 1e3, 2j).

    Con un float de por medio, '/' da lo mismo en Py2 y Py3: no hace falta old_div.
    """
    if node.type != token.NUMBER:
        return False
    value = node.value.lower()
    if value.startswith(("0x", "0o", "0b")):
        return False
    return any(ch in value for ch in ".ej")
