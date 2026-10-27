"""Cargador de plugins legacy con aislamiento.

- Cada plugin corre en su propio modulo, con sus propios __builtins__ (shims Py2).
- Los .py auxiliares que importe el plugin se traducen igual (hook de importacion).
- Un plugin que falla, al cargar o en un callback, se desactiva: Iron3D sigue vivo.
- plugins_patched/<nombre>.py tiene prioridad sobre la traduccion automatica.
- .pypv (cifrado) no se soporta: descifrarlo seria saltarse una proteccion.
"""
from __future__ import annotations

import functools
import hashlib
import importlib.abc
import importlib.util
import logging
import re
import sys
import traceback
import types
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import Any, Callable

from .shims import make_plugin_builtins
from .translator import Py2Translator, TranslationError

log = logging.getLogger("iron3d.py2compat")

# Nombres que el hook nunca intercepta: la API compat, la nativa y la stdlib.
_RESERVED_TOP_LEVEL = {"c4d", "iron", "iron3d_py2compat", "iron3d_legacy"}


class Status(Enum):
    LOADED = "loaded"
    DISABLED = "disabled"      # fallo al cargar o en un callback
    UNSUPPORTED = "unsupported"  # cifrado o tipo de archivo no valido


@dataclass
class PluginRecord:
    path: Path
    status: Status
    reason: str | None = None
    traceback: str | None = None
    translated: bool = False
    patched: bool = False
    old_div_count: int = 0
    module: types.ModuleType | None = field(default=None, repr=False)


def _is_reserved(fullname: str) -> bool:
    top = fullname.partition(".")[0]
    return top in _RESERVED_TOP_LEVEL or top in sys.stdlib_module_names


class _LegacyLoader(importlib.abc.Loader):
    def __init__(self, path: Path, translator: Py2Translator, builtins_ns: dict[str, Any]):
        self.path = path
        self.translator = translator
        self.builtins_ns = builtins_ns

    def create_module(self, spec):
        return None  # modulo por defecto

    def exec_module(self, module):
        result = self.translator.translate_file(self.path)
        module.__dict__["__builtins__"] = self.builtins_ns
        exec(compile(result.source, str(self.path), "exec"), module.__dict__)


class LegacyImportHook(importlib.abc.MetaPathFinder):
    """Resuelve 'import helper' contra las carpetas de plugins, traduciendo al vuelo.

    Va delante en sys.meta_path (como el directorio del script en Py2), pero
    nunca para nombres de la stdlib ni de la API: un 'json.py' dentro de un
    plugin no puede romper Iron3D.
    """

    def __init__(self, translator: Py2Translator, builtins_ns: dict[str, Any]):
        self.translator = translator
        self.builtins_ns = builtins_ns
        self.roots: list[Path] = []
        self.loaded: set[str] = set()

    def add_root(self, root: Path) -> None:
        root = root.resolve()
        if root not in self.roots:
            self.roots.append(root)

    def _under_roots(self, path: Path) -> bool:
        path = path.resolve()
        return any(path == r or r in path.parents for r in self.roots)

    def find_spec(self, fullname, path, target=None):
        if _is_reserved(fullname) or not self.roots:
            return None
        last = fullname.rpartition(".")[2]
        if path is None:
            search = self.roots
        else:
            search = [Path(p) for p in path if self._under_roots(Path(p))]
        for directory in search:
            pkg_init = directory / last / "__init__.py"
            if pkg_init.is_file():
                self.loaded.add(fullname)
                return importlib.util.spec_from_file_location(
                    fullname, pkg_init,
                    loader=_LegacyLoader(pkg_init, self.translator, self.builtins_ns),
                    submodule_search_locations=[str(directory / last)],
                )
            module_file = directory / f"{last}.py"
            if module_file.is_file():
                self.loaded.add(fullname)
                return importlib.util.spec_from_file_location(
                    fullname, module_file,
                    loader=_LegacyLoader(module_file, self.translator, self.builtins_ns),
                )
        return None


class PluginRegistry:
    def __init__(
        self,
        translator: Py2Translator,
        patched_dir: Path | None = None,
        extra_globals: dict[str, Any] | None = None,
    ):
        self.translator = translator
        self.patched_dir = Path(patched_dir) if patched_dir else None
        self.extra_globals = extra_globals or {}
        self.builtins_ns = make_plugin_builtins()
        self.hook = LegacyImportHook(translator, self.builtins_ns)
        self.records: dict[Path, PluginRecord] = {}

    # -- ciclo de vida del hook ------------------------------------------------

    def install(self) -> None:
        if self.hook not in sys.meta_path:
            sys.meta_path.insert(0, self.hook)

    def uninstall(self) -> None:
        if self.hook in sys.meta_path:
            sys.meta_path.remove(self.hook)
        for name in self.hook.loaded:
            sys.modules.pop(name, None)
        self.hook.loaded.clear()

    # -- carga -------------------------------------------------------------------

    def load(self, path: Path) -> PluginRecord:
        path = Path(path).resolve()
        suffix = path.suffix.lower()

        if suffix == ".pypv":
            return self._record(PluginRecord(
                path, Status.UNSUPPORTED,
                reason="plugin cifrado (.pypv): hace falta la version .pyp del autor",
            ))
        if suffix not in (".pyp", ".py"):
            return self._record(PluginRecord(path, Status.UNSUPPORTED, reason=f"tipo no soportado: {suffix}"))

        record = PluginRecord(path, Status.LOADED)
        try:
            patched = self.patched_dir / f"{path.stem}.py" if self.patched_dir else None
            if patched is not None and patched.is_file():
                source = patched.read_text(encoding="utf-8")
                record.patched = True
            else:
                result = self.translator.translate_file(path)
                source = result.source
                record.translated = True
                record.old_div_count = result.old_div_count

            self.hook.add_root(path.parent)
            record.module = self._execute(path, source)
        except TranslationError as e:
            self._fail(record, e)
        except Exception as e:  # el plugin peta al cargar: se aisla
            self._fail(record, e)
        return self._record(record)

    def _execute(self, path: Path, source: str) -> types.ModuleType:
        digest = hashlib.sha256(str(path).encode()).hexdigest()[:8]
        safe = re.sub(r"\W", "_", path.stem)
        modname = f"iron3d_legacy.{safe}_{digest}"

        module = types.ModuleType(modname)
        module.__dict__.update(self.extra_globals)
        module.__file__ = str(path)
        module.__dict__["__builtins__"] = self.builtins_ns
        # C4D ejecuta cada .pyp con __name__ == "__main__" (de ahi el registro en
        # 'if __name__ == "__main__":'). Lo replicamos; la clave en sys.modules es unica.
        module.__name__ = "__main__"

        sys.modules[modname] = module
        try:
            exec(compile(source, str(path), "exec"), module.__dict__)
        except BaseException:
            sys.modules.pop(modname, None)
            raise
        return module

    # -- aislamiento en ejecucion ---------------------------------------------------

    def guarded(self, path: Path, fn: Callable[..., Any], default: Any = None) -> Callable[..., Any]:
        """Envuelve un callback del plugin (Execute, GetVirtualObjects...).

        Si lanza una excepcion: se registra, el plugin queda desactivado y se
        devuelve 'default'. Las siguientes llamadas ya no entran al plugin.
        """
        key = Path(path).resolve()

        @functools.wraps(fn)
        def wrapper(*args, **kwargs):
            record = self.records.get(key)
            if record is not None and record.status is not Status.LOADED:
                return default
            try:
                return fn(*args, **kwargs)
            except Exception as e:
                self.disable(key, e)
                return default

        return wrapper

    def disable(self, path: Path, error: BaseException | str) -> None:
        key = Path(path).resolve()
        record = self.records.get(key) or self._record(PluginRecord(key, Status.LOADED))
        self._fail(record, error)

    # -- internos --------------------------------------------------------------

    def _record(self, record: PluginRecord) -> PluginRecord:
        self.records[record.path] = record
        if record.status is Status.UNSUPPORTED:
            log.warning("Plugin no soportado %s: %s", record.path.name, record.reason)
        return record

    @staticmethod
    def _fail(record: PluginRecord, error: BaseException | str) -> None:
        record.status = Status.DISABLED
        if isinstance(error, BaseException):
            record.reason = f"{type(error).__name__}: {error}"
            record.traceback = "".join(traceback.format_exception(error))
        else:
            record.reason = str(error)
        log.error("Plugin desactivado %s: %s", record.path.name, record.reason)
