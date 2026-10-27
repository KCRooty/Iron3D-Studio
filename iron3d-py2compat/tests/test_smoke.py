"""Smoke tests mínimos — NO son los 23 tests originales de la sesión que generó
este paquete (translator/loader con mocks de c4d, plugins de ejemplo Py2, etc.).
Esos tests y los plugins de ejemplo (classic_cmd, helper_plugin, crashy,
load_crash) estaban en el iron3d-py2compat.zip de la sesión original y no se
han localizado aún en este PC — ver README.md. Esto solo verifica que el
paquete importa y que old_div() reproduce la semántica de división de Py2.
"""
from iron3d_py2compat import old_div


def test_old_div_int_matches_py2_semantics():
    assert old_div(7, 2) == 3
    assert old_div(-7, 2) == -4


def test_old_div_float_is_real_division():
    assert old_div(7, 2.0) == 3.5


def test_old_div_bool_like_py2_true_div_two():
    assert old_div(True, 2) == 0
