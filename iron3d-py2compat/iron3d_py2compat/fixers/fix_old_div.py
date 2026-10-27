"""a / b -> old_div(a, b)

2to3 no toca la division: en Py2 '7 / 2' es 3 y en Py3 es 3.5. Sin este fixer,
un plugin de R19 que reparte clones o calcula indices da resultados distintos
en silencio. old_div() reproduce la semantica de Py2 en tiempo de ejecucion.

Respeta la asociatividad por la izquierda:
    a / b * c  ->  old_div(a, b) * c
    a * b / c  ->  old_div(a * b, c)
"""
from fissix import fixer_base
from fissix.fixer_util import Call, Comma, Name
from fissix.pgen2 import token
from fissix.pygram import python_symbols as syms
from fissix.pytree import Node

from . import FUTURE_DIVISION, is_float_literal


class FixOldDiv(fixer_base.BaseFix):
    BM_compatible = False
    PATTERN = "term< any+ >"
    order = "post"  # primero los terminos anidados, luego los de fuera

    def start_tree(self, tree, filename):
        super().start_tree(tree, filename)
        self.skip_file = bool(FUTURE_DIVISION.search(str(tree)))

    def transform(self, node, results):
        if self.skip_file:
            return None
        children = node.children
        operators = children[1::2]
        if not any(op.type == token.SLASH for op in operators):
            return None

        expr = children[0].clone()
        expr.prefix = ""
        for op, rhs in zip(operators, children[2::2]):
            rhs = rhs.clone()
            if op.type == token.SLASH and not (
                is_float_literal(expr) or is_float_literal(rhs)
            ):
                rhs.prefix = " "
                expr = Call(Name("old_div"), [expr, Comma(), rhs])
            else:
                expr = Node(syms.term, [expr, op.clone(), rhs])
        expr.prefix = node.prefix
        return expr
