"""x /= y -> x = old_div(x, y)

Solo para objetivos simples (nombre, atributo o subindice). El objetivo se
evalua dos veces, igual que en cualquier reescritura de este tipo: un
subindice con efectos secundarios (d[f()] /= 2) es un caso para plugins_patched/.
"""
from fissix import fixer_base
from fissix.fixer_util import Call, Comma, Name
from fissix.pgen2 import token
from fissix.pygram import python_symbols as syms
from fissix.pytree import Leaf, Node

from . import FUTURE_DIVISION, is_float_literal


class FixOldDivAugassign(fixer_base.BaseFix):
    BM_compatible = False
    PATTERN = "expr_stmt< target=any op='/=' value=any >"
    order = "post"

    def start_tree(self, tree, filename):
        super().start_tree(tree, filename)
        self.skip_file = bool(FUTURE_DIVISION.search(str(tree)))

    def transform(self, node, results):
        if self.skip_file:
            return None
        target, op, value = results["target"], results["op"], results["value"]
        if target.type not in (token.NAME, syms.power):
            return None
        if is_float_literal(value):
            return None

        new_target = target.clone()
        left = target.clone()
        left.prefix = ""
        right = value.clone()
        right.prefix = " "
        call = Call(Name("old_div"), [left, Comma(), right], prefix=" ")
        equal = Leaf(token.EQUAL, "=", prefix=op.prefix)
        new = Node(syms.expr_stmt, [new_target, equal, call])
        new.prefix = node.prefix
        return new
