# iron3d-py2compat

Carga plugins `.pyp` de Cinema 4D R19 (Python 2.7) en Python 3, sin embeber un intérprete 2.7.
Todo plugin se traduce siempre (ver `translator.py`): `5 / 2` compila en Python 2 y en Python 3,
pero da resultados distintos (`2` frente a `2.5`), así que "¿compila en Py3?" nunca es una
comprobación válida.

Parte del ecosistema **Iron3D Studio** (Static Forge) — ver `../AGENTS.md` para las reglas
clean-room del proyecto.

## Uso

```
py -m pip install fissix
py -m iron3d_py2compat scan "RUTA\A\TU\CARPETA\plugins"
py -m iron3d_py2compat translate plugin.pyp
```

`scan` solo traduce (no ejecuta nada): dice qué plugins convierten bien, cuántas divisiones
Py2 se han reescrito y cuáles están cifrados (`.pypv`, no soportado — ver Límites).

```python
from iron3d_py2compat import Py2Translator, PluginRegistry, default_cache_dir

registry = PluginRegistry(Py2Translator(default_cache_dir()),
                           patched_dir=Path("plugins_patched"))
registry.install()
record = registry.load(Path("plugins/MiPlugin/MiPlugin.pyp"))
safe_execute = registry.guarded(record.path, command_data.Execute, default=False)
```

## Decisiones de diseño

- Todo plugin de R19 se traduce siempre.
- Cada `.pyp` se ejecuta con `__name__ == "__main__"`, como hace C4D (por eso los plugins
  registran en `if __name__ == "__main__":`). En `sys.modules` va con un nombre único.
- `open()` en modo texto: UTF-8 con `surrogateescape` y `newline=""`, para que los bytes
  leídos y escritos salgan idénticos en Windows y Linux.
- El hook de importación va delante en `sys.meta_path` (como el directorio del script en Py2),
  pero nunca intercepta la stdlib ni `c4d` / `iron`.
- La caché añade un pie de línea al final del archivo traducido para no mover los números de línea.
- Caché por usuario: `%LOCALAPPDATA%\Iron3D\py2cache` en Windows, `~/.cache/iron3d/py2cache` en Linux.

## Límites conocidos

- `d[f()] /= 2` evalúa `f()` dos veces. Si un plugin depende de eso, se corrige a mano en
  `plugins_patched/`.
- Diferencias de comportamiento de la stdlib entre Py2 y Py3 (por ejemplo, el orden de los
  dict) no se emulan.
- `.pypv` (plugins cifrados) no soportado: descifrarlo sería saltarse una protección.

## Tests

23 tests pasando sobre Python 3.13 en la sesión original (ver el documento maestro del
proyecto, sección 7.4, para la tabla completa de garantías cubiertas). El `.zip` con los
tests, los plugins de ejemplo y este README tal como se generaron la primera vez vive en
el PC de Core — pendiente de localizar y copiar aquí sobre este esqueleto (ver `tests/`).
