"""Traductor Python 2 -> Python 3 para plugins legacy (.pyp / .py).

Motor: fissix (fork mantenido de lib2to3, que desaparecio en Python 3.13)
+ fixers propios de Iron3D (division con semantica Py2).

Todo plugin de R19 es Python 2.7, asi que se traduce SIEMPRE: comprobar si
"compila en Py3" no sirve, porque '5 / 2' compila en ambos y da distinto.
"""
from __future__ import annotations

import hashlib
import io
import os
import re
import tempfile
import threading
import tokenize
from dataclasses import dataclass
from pathlib import Path

from fissix.pgen2.parse import ParseError
from fissix.pgen2.tokenize import TokenError
from fissix.refactor import RefactoringTool, get_fixers_from_package

TRANSLATOR_VERSION = "1"

_OWN_FIXERS = [
    "iron3d_py2compat.fixers.fix_old_div",
    "iron3d_py2compat.fixers.fix_old_div_augassign",
]

_FOOTER = "# iron3d-py2compat v{version} | origen: {origin} | tabs_normalized={tabs}\n"
_FOOTER_RE = re.compile(r"^# iron3d-py2compat v\S+ \|.*tabs_normalized=(\d)\s*$", re.M)
_MIXED_INDENT = re.compile(r"^(?=[ \t]*\t)[ \t]+", re.M)


class TranslationError(Exception):
    """El plugin no se pudo traducir. Lleva archivo y linea para el usuario."""

    def __init__(self, filename: str, message: str, lineno: int | None = None):
        self.filename = filename
        self.lineno = lineno
        where = f"{filename}:{lineno}" if lineno else filename
        super().__init__(f"{where}: {message}")


@dataclass
class TranslationResult:
    source: str
    cache_hit: bool
    cache_path: Path | None
    old_div_count: int
    tabs_normalized: bool


def default_cache_dir() -> Path:
    """Cache por usuario, misma logica en Windows y Linux."""
    if os.name == "nt":
        base = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData" / "Local"))
        return base / "Iron3D" / "py2cache"
    base = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache"))
    return base / "iron3d" / "py2cache"


def decode_source(raw: bytes) -> str:
    """Respeta la cabecera '# -*- coding: ... -*-' de Py2; si no, UTF-8; si falla, latin-1."""
    try:
        encoding, _ = tokenize.detect_encoding(io.BytesIO(raw).readline)
    except SyntaxError:
        encoding = "utf-8"
    try:
        text = raw.decode(encoding)
    except (UnicodeDecodeError, LookupError):
        text = raw.decode("latin-1")
    return text.lstrip("﻿")


def normalize_tabs(source: str) -> str:
    """Indentacion mixta tab/espacios -> espacios, con la regla de Py2 (tab = hasta multiplo de 8).

    Py2 lo aceptaba; Py3 lanza TabError. Solo toca el espaciado inicial de cada linea.
    """
    return _MIXED_INDENT.sub(lambda m: m.group(0).expandtabs(8), source)


class Py2Translator:
    def __init__(self, cache_dir: Path | None = None):
        self.cache_dir = Path(cache_dir) if cache_dir else None
        fixers = get_fixers_from_package("fissix.fixes") + _OWN_FIXERS
        self._tool = RefactoringTool(fixers)
        self._lock = threading.Lock()  # RefactoringTool no es thread-safe

    # -- API ---------------------------------------------------------------

    def translate_source(self, source: str, filename: str = "<plugin>") -> tuple[str, bool]:
        """Devuelve (codigo_py3, tabs_normalizadas). Lanza TranslationError."""
        if not source.endswith("\n"):
            source += "\n"
        try:
            return self._translate_checked(source, filename), False
        except TranslationError:
            if not _MIXED_INDENT.search(source):
                raise
            return self._translate_checked(normalize_tabs(source), filename), True

    def translate_file(self, path: Path) -> TranslationResult:
        path = Path(path)
        raw = path.read_bytes()
        cache_path = self._cache_path(path, raw)
        if cache_path is not None and cache_path.is_file():
            source = cache_path.read_text(encoding="utf-8")
            match = _FOOTER_RE.search(source)
            return TranslationResult(
                source=source,
                cache_hit=True,
                cache_path=cache_path,
                old_div_count=source.count("old_div("),
                tabs_normalized=bool(match and match.group(1) == "1"),
            )

        original = decode_source(raw)
        source, tabs = self.translate_source(original, str(path))
        # El pie va al FINAL para no mover numeros de linea en los tracebacks.
        source += _FOOTER.format(version=TRANSLATOR_VERSION, origin=path.name, tabs=int(tabs))
        if cache_path is not None:
            self._atomic_write(cache_path, source)
        return TranslationResult(
            source=source,
            cache_hit=False,
            cache_path=cache_path,
            old_div_count=source.count("old_div(") - original.count("old_div("),
            tabs_normalized=tabs,
        )

    # -- internos ------------------------------------------------------------

    def _translate_checked(self, source: str, filename: str) -> str:
        try:
            with self._lock:
                tree = self._tool.refactor_string(source, filename)
        except ParseError as e:
            lineno = None
            if isinstance(e.context, tuple) and len(e.context) == 2:
                lineno = e.context[1][0]
            raise TranslationError(filename, f"sintaxis Py2 no valida ({e.msg})", lineno) from e
        except (TokenError, IndentationError) as e:
            raise TranslationError(filename, f"error de tokenizado: {e}") from e

        result = str(tree)
        try:
            compile(result, filename, "exec")
        except SyntaxError as e:
            raise TranslationError(filename, f"resultado no compila en Py3: {e.msg}", e.lineno) from e
        return result

    def _cache_path(self, path: Path, raw: bytes) -> Path | None:
        if self.cache_dir is None:
            return None
        digest = hashlib.sha256(TRANSLATOR_VERSION.encode() + b"\0" + raw).hexdigest()[:32]
        return self.cache_dir / f"{path.stem}-{digest}.py"

    @staticmethod
    def _atomic_write(target: Path, content: str) -> None:
        target.parent.mkdir(parents=True, exist_ok=True)
        fd, tmp = tempfile.mkstemp(dir=target.parent, suffix=".tmp")
        try:
            with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as fh:
                fh.write(content)
            os.replace(tmp, target)
        except BaseException:
            Path(tmp).unlink(missing_ok=True)
            raise
