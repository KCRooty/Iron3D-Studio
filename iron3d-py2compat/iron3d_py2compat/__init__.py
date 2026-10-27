"""iron3d-py2compat — API pública.

NOTA: este __init__.py no venía con código fuente explícito en el documento
maestro del proyecto (solo el ejemplo de uso de más abajo); se escribe aquí
por inferencia directa de ese ejemplo y de los nombres usados en shims.py,
translator.py y loader.py. Revisar contra el .zip original si aparece.

Uso:
    from iron3d_py2compat import Py2Translator, PluginRegistry, default_cache_dir

    registry = PluginRegistry(Py2Translator(default_cache_dir()),
                               patched_dir=Path("plugins_patched"))
    registry.install()
    record = registry.load(Path("plugins/MiPlugin/MiPlugin.pyp"))
    safe_execute = registry.guarded(record.path, command_data.Execute, default=False)
"""
from .loader import LegacyImportHook, PluginRecord, PluginRegistry, Status
from .shims import PY2_SHIMS, make_plugin_builtins, old_div
from .translator import (
    Py2Translator,
    TranslationError,
    TranslationResult,
    default_cache_dir,
)

__all__ = [
    "Py2Translator",
    "TranslationError",
    "TranslationResult",
    "default_cache_dir",
    "PluginRegistry",
    "PluginRecord",
    "Status",
    "LegacyImportHook",
    "make_plugin_builtins",
    "old_div",
    "PY2_SHIMS",
]
