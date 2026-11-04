# Cambios de Iron3D Studio

Todavía no hay versiones numeradas: Iron3D está en desarrollo temprano y nada de esto es
una versión terminada. Cada bloque lleva la fecha en que entró, qué se probó y qué no.
Las notas largas, en tono de blog, están en [`docs/devlog/`](docs/devlog/).

## Sin publicar

### 2026-11-04 — Importar escenas de Cinema 4D (.c4d)

**Añadido**
- **Importador `.c4d`** con el Cineware SDK de Maxon, solo en Windows. El SDK no está en el
  repo: se indica con `-DIRON3D_CINEWARE_SDK=<ruta>`. Sin el SDK se compila un stub que
  explica por qué no se puede abrir el archivo.
  - Jerarquía, nombres, posición, rotación, escala y visibilidad en el editor.
  - Objetos poligonales (con la malla deformada si C4D la guardó) y suavizado Phong
    según el ángulo de la etiqueta Phong.
  - Primitivas reconstruidas desde sus parámetros: cubo, esfera, plano, cilindro, cono,
    toroide, disco, pirámide y polígono.
  - Generadores con la geometría guardada por C4D: entran como malla, y sus hijos (las
    entradas del generador) quedan ocultos.
  - Materiales: color y brillo, y rugosidad y metal desde la primera capa de
    reflectancia. Se heredan por la jerarquía y vale la última etiqueta de textura, como
    en C4D.
  - Luces (posición).
- **Archivo > Abrir** (Ctrl+O) con el diálogo del sistema, para `.iron` y `.c4d`.
- **Archivo > Fusionar** (Ctrl+Mayús+O): añade un `.c4d` o un `.iron` a la escena actual.
  Se puede deshacer.
- Ventana de informe al importar: qué entró (objetos, mallas, materiales, polígonos) y
  qué no, agrupado por tipo.
- `Iron3DStudio.exe archivo.c4d` (o `.iron`) abre el archivo al arrancar y encuadra la
  escena.
- Nuevo tipo de objeto **Polígono** (malla importada): selección por triángulo en el
  visor, contorno de selección, y vértices y triángulos en Atributos.
- Formato `.iron` versión 3: guarda las mallas importadas. El historial de deshacer
  comparte la malla entre pasos en vez de copiarla.
- `tools/c4d_probe`: importa un `.c4d` sin abrir ventana y lista lo que sale. Sirve para
  pruebas.
- Primer test automático (`iron3d_tests`, con `ctest`): geometría de las primitivas y
  conversión de coordenadas de C4D. La CI lo ejecuta en Windows y en Linux.

**Cambiado**
- Índices de malla de 16 a 32 bits: las mallas de C4D pasan de 65 536 vértices.
- Vulkan: las mallas se destruyen 4 frames más tarde, cuando la GPU ya no las usa, y los
  huecos libres se reutilizan.

**Arreglado**
- Si fallaba la importación de la escena pasada por línea de comandos, la selección
  apuntaba a un objeto borrado.

**Probado**
- Compilación con MSVC (con y sin el SDK) y con GCC en Linux (Debian en WSL), sin avisos
  nuevos. El test de geometría pasa.
- Importación con `c4d_probe` de archivos reales de DONNIES (`test.c4d`, `jumpscare.c4d`)
  y de escenas de ejemplo de R19. La más grande tiene 121 objetos, 573 368 polígonos y
  14 materiales, y tarda 3,6 s en Debug.
- Capturas de Iron3D abierto con `jumpscare.c4d`: escena, jerarquía, materiales e informe
  correctos.
- `jumpscare.c4d` → `.iron` con `c4d_probe --save`, releído y abierto en Iron3D con la
  misma escena.

**Sin probar**
- La ruta de generadores con geometría guardada: ningún archivo de prueba la tenía.
- Abrir y Fusionar desde el menú con el ratón.

**Problemas conocidos**
- El `.iron` es texto. Con una escena grande ocupa mucho y carga despacio: el de
  `jumpscare` ocupa 201,7 MB y tarda casi un minuto en abrir en Debug, con la ventana en
  blanco mientras tanto.
- Todavía no se importan cámaras, splines, deformadores, objetos de escena (cielo, suelo,
  entorno...) ni generadores sin geometría guardada (Subdivision Surface, Simetría...).
  Entran como Nulo, con aviso.
- Tampoco se importan UV, texturas ni animación.
- En Linux no hay importador: Cineware solo trae librerías para Windows y macOS.

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
