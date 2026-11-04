# Cada icono, el suyo, y cinco formas de vestir la interfaz

*4 de noviembre de 2026 · Static Forge*

Hasta ayer, si abrías la paleta de deformadores de Iron3D veías 29 veces el mismo
dibujo: un rectángulo lila doblado. Con las splines, los generadores, la escena, las
cámaras y las luces pasaba igual. Cada grupo tenía un icono y lo repetía en todas las
entradas. Funcionaba, pero no parecía un programa 3D: parecía una lista.

Esta tanda arregla eso y añade una cosa que llevábamos tiempo queriendo: presets de
interfaz. Es un refresh, no un rework. Los paneles son los mismos, los menús internos
son los mismos y todo lo que hacía Iron3D lo sigue haciendo igual.

## 222 iconos, todos dibujados en código

Iron3D no usa ni un solo recurso gráfico de Maxon ni de ningún otro programa. Cada
icono es un puñado de líneas, polígonos y degradados pintados con `ImDrawList` a partir
de coordenadas normalizadas. Así se ve igual de nítido a 16 px en un menú que a 20 en
una paleta, y no hay ningún PNG que mantener.

Hemos pasado de unos 85 a **222**:

- **Splines**: lápiz, dibujo y suavizado en naranja; las primitivas (arco, círculo,
  hélice, estrella, texto, rueda dentada, flor, perfil...) en trazo azul claro, y las
  cinco booleanas (sustraer, unión, Y, O, intersección) en gris.
- **Generadores y Modelado**, en verde: subdivisión, extrusión, torno, forro,
  recorrido; matriz, booleano, simetría, metaball, LOD, reducción de polígonos...
- **MoGraph**: clonador, matriz, fractura, Voronoi, MoText, trazador, PolyFX...
- **Deformadores**: los 29, en la familia lila-azul, cada uno con su metáfora (doblar
  es una columna curvada, enroscar una columna retorcida, viento unas líneas
  onduladas...).
- **Escena, cámaras y luces**: cielo, entorno, escenario, nubes, césped; cámara con
  objetivo, estéreo, grúa; foco, IES, área, solar, PBR...
- **Menús**: nuevo, abrir, guardar, cortar, copiar, pegar, selección rectangular,
  lazo, región de render, cola de render, cada gestor...

Las paletas siguen ahora el orden de Cinema 4D R19 fila a fila, llevan la tira rayada
de arriba y una franja oscura detrás de la columna de iconos. El cuarto grupo de la
barra superior pasa a ser **Modelado** (con el icono de Matriz), como en R19.

Casi todos esos objetos siguen saliendo deshabilitados con "todavía no implementado".
Tener el icono no significa tener el objeto. Preferimos que se vea qué falta antes que
fingir que funciona.

## Presets de interfaz

En **Preferencias > Interfaz** hay ahora una galería con cinco presets: **Cinema 4D,
Blender, Maya, 3ds Max y Source Filmmaker**. Al elegir uno, la interfaz cambia en el
momento:

- **Colores**: fondo de paneles, campos, acento, azul de herramientas, texto, color de
  selección y fondo del visor. Los de Blender, Maya y SFM están muestreados de capturas
  reales de esos programas; los de 3ds Max son una aproximación a su tema oscuro.
- **Colocación de los paneles.** En Blender, Esquema y Propiedades van a la derecha y
  la línea de tiempo abajo del todo. En Maya, el Outliner tiene su propia columna a la
  izquierda y el Channel Box ocupa toda la derecha. En 3ds Max, el Scene Explorer va a
  la izquierda y el Command Panel a la derecha. En SFM, el Animation Set Editor va a la
  izquierda y debajo del visor hay una línea de tiempo enorme.
- **Nombres**: cada panel se llama como en el programa imitado, en una ficha junto al
  asa del panel y en el menú Ventana.
- **Métricas**: el redondeo y el relleno de los controles.

Lo que no cambia: el contenido de cada panel, el menú principal y los menús internos.
Eso ya sería rehacer la interfaz, y no era la idea.

Cada tarjeta de la galería tiene una miniatura. No es una imagen: es la misma función
que coloca los paneles de verdad (`computeLayout`), escalada y pintada con los colores
del preset. Si mañana cambia la disposición, la miniatura cambia sola.

Debajo de la galería se puede **personalizar** todo: los 7 colores, la disposición
base, la altura de la barra, el ancho de las columnas, el reparto entre objetos y
atributos, la fila de abajo, la línea de tiempo, las pestañas verticales y el redondeo.
En cuanto tocas algo, el preset pasa a llamarse "Personalizado (sin guardar)", y con
**Guardar como preset** queda como uno tuyo en `presets/<nombre>.i3dui`.

El preset de Cinema 4D reproduce exactamente la interfaz de antes, con los mismos
números. Es el que sale por defecto, y un `preferences.cfg` antiguo arranca con él.

## Cómo lo hemos comprobado

Sin mover el ratón por el usuario: lanzamos el programa, capturamos la ventana con
`PrintWindow` y la cerramos. Para eso hay tres flags nuevos:

- `--icon-sheet` abre una hoja con los 222 iconos y su nombre.
- `--ui-preset=<c4d|blender|maya|max|sfm>` arranca con ese preset sin guardarlo.
- `--prefs` abre Preferencias al arrancar.

Con eso se revisaron la hoja de iconos, los cinco presets, la galería y la
persistencia: un `preferences.cfg` con solo `ui.preset=max` arranca como 3ds Max.

## Lo que falta

- Probar la galería con el ratón: clics, deslizadores, guardar y eliminar presets
  propios, y que se mantengan tras reiniciar.
- Probar con la ventana muy pequeña. En el código ningún panel baja de 1 px, pero no
  hay captura.
- En Maya y 3ds Max la columna de objetos es estrecha y su menú interno se corta. Se
  puede ensanchar desde Personalizar.
- Los iconos son los mismos en todos los presets. Cambiar el juego entero por programa
  sería otro proyecto.

El cambio entero está en [`CHANGELOG.md`](../../CHANGELOG.md).
