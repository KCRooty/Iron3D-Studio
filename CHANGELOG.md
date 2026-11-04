# Cambios de Iron3D Studio

Todavía no hay versiones numeradas: Iron3D está en desarrollo temprano y nada de esto es
una versión terminada. Cada bloque lleva la fecha en que entró, qué se probó y qué no.
Las notas largas, en tono de blog, están en [`docs/devlog/`](docs/devlog/).

## Sin publicar

### 2026-11-04 — Refresh de la interfaz: iconos propios y presets de interfaz

**Añadido**
- 222 iconos vectoriales dibujados en código (antes ~85). Cada objeto y herramienta de
  las paletas de Crear tiene su propio dibujo, en vez del icono genérico del grupo. Las
  familias están en `src/ui/Icons*.cpp`, con el lápiz común en `IconPen.h`.
- Presets de interfaz en Preferencias > Interfaz: **Cinema 4D, Blender, Maya, 3ds Max
  y Source Filmmaker**. Cada uno cambia los colores, el redondeo, la colocación de los
  paneles y su nombre (Esquema, Outliner, Scene Explorer, Animation Set Editor...). El
  contenido de los paneles no cambia.
- Galería de presets con una miniatura de cada disposición. Debajo se pueden
  personalizar los 7 colores, la disposición base, el tamaño de los paneles, las
  pestañas verticales y el redondeo.
- Presets propios: guardar, eliminar y restablecer (`presets/<nombre>.i3dui`).
- Iconos en los menús principales (Archivo, Edición, Crear, Selección, Herramientas,
  Animación, Render, Ventana, Ayuda).
- Grupo **Modelado** en la barra superior (Matriz, Booleano, Simetría, LOD...), como en
  R19. MoGraph sigue en su menú.
- Flags de verificación: `--icon-sheet`, `--ui-preset=<id>` y `--prefs`.

**Cambiado**
- Las paletas siguen el orden de R19: herramienta Spline Arco, operaciones booleanas de
  splines, nubes y césped, y todos los tipos de cámara y luz.
- El color de selección y el fondo del visor pasan a formar parte del tema.
- `preferences.cfg` guarda el preset entero. Un archivo antiguo arranca como Cinema 4D.

**Probado**: compilación MSVC /W4 sin avisos. Hay capturas de la hoja de iconos, de los
5 presets y de la página de Preferencias, y se probó la persistencia con
`ui.preset=max`.
**Sin probar**: la galería con el ratón (clics, deslizadores, guardar y eliminar
presets) y una ventana muy pequeña.

### 2026-11-03 — Oclusión ambiental y colores de la interfaz

- **SSAO** (solo Vulkan): 16 muestras en hemisferio y 3 pasadas (G-buffer, SSAO y
  desenfoque). Solo oscurece el término ambiente, nunca la luz directa. Ver
  `ROADMAP_RENDER.md`.
- **Colores de la interfaz** editables en Preferencias > Interfaz: unas pocas semillas
  de las que se deriva toda la paleta (`UiTheme`). El tema por defecto es el de siempre.
- **Sin probar** a fondo: la comparación visual del SSAO con un objeto apoyado en un
  plano.

### 2026-10-30 — Render PBR, sombras, selección múltiple y modos libres del gizmo

- Materiales **PBR** (Cook-Torrance: GGX + Smith + Fresnel-Schlick, metálico/rugosidad)
  y luz de ambiente de hemisferio cielo/suelo (solo Vulkan; OpenGL sigue en Phong).
- **Sombras** del primer foco de la escena: shadow map ortográfico con PCF 3x3. Probado
  en vivo con un cubo sobre un plano.
- **Selección múltiple**: Mayús+clic en el visor y Ctrl+clic en el Gestor de Objetos,
  con el gizmo en el centro de la selección.
- **Gizmo**: anillo de rotación paralelo a la vista, rotación libre tipo trackball
  (arcball de Shoemake) y modos libres de Mover/Escalar al arrastrar fuera de los ejes.
  **Pendiente** de una prueba a fondo con el ratón.
- El formato `.iron` guarda `metallic`/`roughness` en vez de `specular`/`shininess`,
  sin migración.
