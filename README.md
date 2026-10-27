# Iron3D Studio

DCC 3D open source (Static Forge) que replica el flujo de trabajo de Cinema 4D R19 —
código 100% propio, reimplementación clean-room — con evaluación en GPU (Vulkan),
compatibilidad con `.c4d`/`.lib4d` y plugins de R19.

**Antes de tocar nada:** lee `AGENTS.md` (reglas clean-room, no negociables) y el
Documento Maestro del proyecto en `V:\ObsidianVault\14-Iron3D-CoalMix\Iron3D_CoalMix_Documento_Maestro.md`.

Estado: fase de diseño (2026-10-27). Sin código C++ todavía — ver `AGENTS.md` §5 para el
stack y el checklist de "Tareas inmediatas" en el Documento Maestro §8.5.

## Estructura

```
src/
  core/            ECS, Data-Oriented Design, grafo de escena (TRS)
  gfx/              VulkanDevice, VMA, VulkanPipeline, VulkanCompute, VulkanRayTracing
  iron_compute/     Clonadores e instancing masivo en compute shaders
  ui/               Paneles ImGui
  sdk/              Iron3D SDK (API C++ de plugins)
  python/           CPython embebido + módulo `iron` (pybind11)
  importers/
    melange/        Lector/escritor .c4d (módulo opcional, revisar licencia)
    lib4d/          Parser de .lib4d -> .ilib
    c4d_compat/     Módulo `c4d` + ABI .cdl64 (fase tardía)
tools/
  dump_c4d_api.py   Ejecutar DENTRO de R19 (Script Manager) — Python 2.7
iron3d-py2compat/   Paquete Python 3 para cargar plugins .pyp de R19 (hecho, 23 tests
                    originales pendientes de localizar — ver su README.md)
shaders/            GLSL -> SPIR-V
tests/oracle/       Casos comparados contra C4D R19 (caja negra)
docs/api/           c4d_r19_api.json (salida de dump_c4d_api.py)
assets/             Presets .ilib, iconos y temas propios
```

## Licencia

MIT OR Apache-2.0 (por confirmar antes del primer commit público).
