# AGENTS.md — Iron3D Studio

Reglas obligatorias para cualquier agente (Claude Code, etc.) o colaborador humano.
Iron3D Studio es un DCC 3D open source de Static Forge que replica el flujo de trabajo
de Cinema 4D R19 con código propio, y lo mejora.

## 1. Clean-room (no negociable)

- Prohibido descompilar, desensamblar o depurar binarios de Cinema 4D (Ghidra, IDA, x64dbg…).
- Prohibido copiar código, shaders, presets, materiales, texturas, iconos, skins o assets de Maxon.
- Prohibido usar las marcas "Cinema 4D", "MoGraph", "Maxon" como nombre de nada en el producto.
  En docs se permite solo de forma descriptiva ("compatible con archivos de Cinema 4D").
- Permitido como referencia:
  - Uso del programa, grabaciones de pantalla, documentación pública y manuales.
  - Observación entrada → salida (el "oráculo", ver §4).
  - Cineware SDK oficial de Maxon (antes "Melange SDK", renombrado por Maxon en 2019 --
    mismo SDK, nombre nuevo) para leer/escribir .c4d, aislado en src/importers/cineware/
    como módulo opcional (revisar su licencia antes de enlazarlo; el SDK en si lee Y
    escribe .c4d, no es solo lector).
- Si una tarea solo puede hacerse mirando código de Maxon: parar y avisar, no improvisar.
- **Excepción registrada 2026-10-27:** Core autorizó explícitamente, tras aviso del riesgo legal, usar
  RAE (ingeniería reversa/decompilación) sobre el binario real de C4D R19 instalado — esto excede la
  regla de "solo caja negra" de este archivo. Alcance real usado: strings + un dato arquitectónico
  (`gui.module.xdl64` usa `maxon.HierarchyObjectInterface` con notificaciones Observer
  `ObservableHierarchyInsert`/`Remove` para el árbol del Gestor de Objetos) — coincide con el SDK
  público de Maxon, no se decompiló pseudocódigo de algoritmos. No se copió ningún identificador,
  cadena ni estructura de Maxon al código de Iron3D; el grafo de escena de la Fase 2
  (`src/core/SceneNode.*`) es diseño propio desde cero. Si se repite esta excepción, anotar aquí
  qué binario y qué se miró exactamente.

## 2. Qué se replica 1:1 y qué se mejora

| Replicar 1:1 (memoria muscular del usuario) | Mejorar libremente |
|---|---|
| Distribución de paneles: Gestor de Objetos, Atributos, Materiales, Timeline, Visor | Evaluación en GPU (IronCompute) en vez de un solo núcleo |
| Atajos de teclado y modos (modelo/puntos/aristas/polígonos) | Render Vulkan RT / OptiX en vez de ProRender |
| Jerarquía padre-hijo y comportamiento de generadores/deformadores | Rendimiento del visor a 144Hz+ |
| Semántica de clonadores (lineal, radial, grid, objeto) y efectores | Sistema nodal IronNodes |
| Abrir .c4d de R19 con resultado visual equivalente | Features nuevas de Static Forge |

Equivalencia = visual y de comportamiento, no byte a byte.

## 3. Contenido propio

Todo preset, material, librería .ilib, icono y tema se crea desde cero o se toma de fuentes con
licencia libre. Cada asset externo se lista en ASSETS.md con su licencia.

## 4. Oráculo de comportamiento

Core tiene licencia legal de C4D R19 instalada. Se usa solo como caja negra:
1. Escena de prueba → render/export en R19 → tests/oracle/<caso>/expected.*
2. Misma escena en Iron3D → comparar (diff de imagen, transforms, conteo de clones).
3. Los casos de test describen el comportamiento esperado en texto, sin código de Maxon.

## 5. Stack

C++20/23 · CMake · Vulkan 1.3 + VMA · Dear ImGui · GLFW · GLM · Assimp · SPIR-V.
Windows (MSVC/Clang-cl) y Linux (GCC/Clang) con paridad; CI falla si uno de los dos no compila.

## 6. Calidad

- Sin abort()/crash en código de release: errores propagados y mostrados al usuario.
- Formato .iron versionado desde el primer commit.
- Cada feature nueva: test + entrada en CHANGELOG.md.
- Cero telemetría. Sin anuncios. Sin paywalls.

## 7. Licencia

MIT OR Apache-2.0 (dual, por decidir con el primer commit). Las dependencias deben ser compatibles.
Revisar la licencia de Cineware SDK antes de enlazarlo (puede obligar a aislarlo como módulo
descargable aparte en vez de incluirlo en el repo).
