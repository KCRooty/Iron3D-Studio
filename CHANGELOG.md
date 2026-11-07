# Cambios de Iron3D Studio

Todavía no hay versiones numeradas: Iron3D está en desarrollo temprano y nada de esto es
una versión terminada. Cada bloque lleva la fecha en que entró, qué se probó y qué no.
Las notas largas, en tono de blog, están en [`docs/devlog/`](docs/devlog/).

## Sin publicar

### 2026-11-07 (6) — Polígonos de cuatro lados en los modos de componentes

**Añadido**
- Los modos Puntos/Aristas/Polígonos ven la malla como C4D: dos triángulos consecutivos,
  planos, que comparten arista y forman un cuadrilátero convexo son **un solo polígono**.
  Las diagonales internas ya no salen como aristas. Un cubo editable da 8 puntos,
  12 aristas y 6 polígonos.
- Clic, Rectangular/Lazo y Supr trabajan con esos polígonos. Al borrar se quitan también
  los vértices que quedan sueltos.
- Las mallas pueden guardar sus polígonos (cuántos triángulos forma cada uno, también
  n-gonos). Conectar y Soldar los conservan. Se usará en las herramientas de modelado
  para que un corte no se vuelva a juntar mal.
- Nueva topología en `src/core/MeshTopology.*`, con tests.

**Cambiado**: el formato `.iron` pasa a la versión 6 (bloque `FACES` opcional tras
`MESH`). Las escenas anteriores se abren igual; una versión anterior de Iron3D no abre
las nuevas que traigan `FACES`.

**Probado**: compila sin avisos y pasan los tests:
- cubo: 8/12/6;
- plano: 1 cuadrilátero;
- esfera cerrada (V − A + P = 2);
- borrar una cara;
- pentágono explícito;
- ida y vuelta de `FACES`.

**Sin probar**: no hay verificación limpia por captura. Mientras capturaba, el usuario
estaba usando el PC y sus clics entraron en la ventana. En esas capturas se vieron
«6 polígonos» y «5 tras borrar», pero no las cuento como prueba. Nada probado con el
ratón ni con OpenGL.

**Limitaciones conocidas**:
- Solo se deducen cuadriláteros de triángulos **consecutivos**. Una malla importada
  con otro orden se queda en triángulos.
- Los n-gonos deducidos no existen: solo los guardados en `FACES`.

### 2026-11-07 (5) — Modos Puntos, Aristas y Polígonos

**Añadido**
- Los botones **Puntos / Aristas / Polígonos** de la barra izquierda ya funcionan con
  objetos editables (Objeto Polígono: importados o convertidos con C). Modelo vuelve a
  editar objetos enteros.
- Se dibujan los componentes del objeto activo encima del visor. Los puntos son los
  vértices unidos por posición: un cubo de 24 vértices de render da 8 puntos.
- **Selección**: con clic, Mayús+clic para añadir o quitar y Ctrl+clic para quitar. Con
  Rectangular y Lazo, la opción Tolerante decide si basta un punto dentro o hacen falta todos.
- El **gizmo** mueve, gira y escala lo seleccionado alrededor de su centro, con los ejes
  del objeto. Las normales de los triángulos tocados se recalculan: las aristas vivas
  siguen vivas. Se puede deshacer.
- **Supr** borra los polígonos seleccionados, o los que usan los puntos o aristas
  seleccionados.

**Probado**: compila sin avisos, pasan los tests y está verificado por captura. Se hizo
editable el Cubo, que dio 8 puntos. En modo Polígonos, 4 triángulos salen en naranja y el
gizmo queda en su centro; en modo Puntos, 3 puntos seleccionados.
**Sin probar**: nada con el ratón de verdad (clic, arrastre del gizmo, marco, Supr) ni con
OpenGL.

**Limitaciones conocidas**:
- Se dibujan también las aristas ocultas, porque no hay oclusión.
- Los polígonos son triángulos: un quad de C4D aparece como dos.
- En estos modos el clic en el visor no cambia de objeto; eso se hace en el Gestor de Objetos.
- No dibuja componentes en mallas de más de 60 000 puntos o aristas.
- No hay herramientas de modelado: ni extruir, ni biselar, ni cuchillo.

### 2026-11-07 (4) — Animación por fotogramas clave (F9)

**Añadido**
- **Grabar Objetos Activos (F9)**: graba una clave de los objetos seleccionados en el
  fotograma actual. Hay una pista por canal (Posición, Rotación, Escala), y los botones
  Grabar Posición/Escala/Rotación de la Línea de Tiempo eligen qué canales se graban.
- **Reproducción** (F8/F6 o la Línea de Tiempo): al cambiar de fotograma los objetos toman
  los valores de sus pistas. Entre claves la curva es spline con tangentes automáticas que
  no rebasan las claves (dos claves dan una S con aceleración y frenada). Antes de la
  primera clave y después de la última el valor se queda fijo. Lo que se mueve sin grabar
  se pierde al cambiar de fotograma, como en C4D.
- **Fotogramas Clave Automáticos (Ctrl+F9)**: al soltar el gizmo se graba la clave sola.
- **Ir a la Clave Anterior/Siguiente (Ctrl+F / Ctrl+G)** y las claves del objeto activo
  marcadas en la regla.
- **Animación > Eliminar Animación del Objeto**. Grabar y eliminar se pueden deshacer.
- Duplicar copia también las pistas.

**Cambiado**: el formato `.iron` pasa a la versión 5 (guarda las claves). Las escenas
anteriores se abren igual, pero una versión anterior de Iron3D no abre las nuevas.

**Limitaciones conocidas**: no hay editor de curvas ni forma de mover o borrar una clave
suelta, y Grabar Parámetro y Nivel de Puntos siguen sin hacer nada.

**Probado**: build Debug sin avisos, ctest 2/2 (pruebas nuevas: claves por canal, curva
en S, sin rebasar, valores fijos fuera del rango, grabar dos veces el mismo fotograma,
guardar y leer el `.iron` y Duplicar), captura de una clave grabada en el fotograma 45.
**Sin probar**: reproducir una animación viéndola en el visor y las claves automáticas al
arrastrar.

### 2026-11-07 (3) — Ruedas de opciones en los menús

**Añadido**
- **Rueda dentada de opciones** a la derecha de algunas entradas de menú, como en C4D R19:
  clic en el texto ejecuta la orden y clic en la rueda abre sus opciones.
  - **Selección Rectangular / con Lazo**: opción **Tolerante** (activada por defecto: basta
    con tocar la zona; desactivada: el objeto tiene que quedar dentro entero).
  - **Conectar Objetos**: **Soldar** (activada) con **Tolerancia** en cm: une los puntos que
    coinciden en posición, normal y color, y quita los triángulos que quedan degenerados.
  - **Renderizar Vista Activa** y **Renderizar en Visor de Imágenes**: la rueda abre la
    Configuración de Render.

**Probado**: build Debug sin avisos, ctest 2/2, capturas del menú del visor con las ruedas,
de la ventana de opciones de Conectar y de Conectar+Eliminar con la soldadura activada
(228 vértices). **Sin probar**: clic real sobre la rueda y la opción Tolerante arrastrando
la zona.

### 2026-11-07 (2) — Ejes de objeto/mundo (W), Activar Eje (L), render en Visor de Imágenes y Configuración de Render

**Añadido**
- **Sistema de coordenadas (W)**: el gizmo usa ahora los ejes del objeto, como C4D por
  defecto; W (o el botón de la barra) cambia a los del mundo y vuelta.
- **Activar Eje (L)**: mover, rotar o escalar cambia solo el eje del objeto. Los hijos se
  quedan donde estaban y, en un objeto editable (Objeto Polígono), también la malla. En una
  primitiva paramétrica se mueve el objeto entero, como en C4D.
- **Renderizar en Visor de Imágenes (Mayús+R)**: render por CPU de la cámara activa, con la
  visibilidad de render de cada objeto, en una ventana propia con zoom (ajustar, 50, 100,
  200 %), "Renderizar" otra vez y **Archivo > Guardar como...** PNG.
- **Editar Configuración de Render (Ctrl+B)**: resolución (con preestablecidos o la del
  visor), antialiasing de 1x1 a 4x4 y fondo en degradado. También en el menú Render, la
  barra superior y el Commander.

**Cambiado / arreglado**
- Rotar con los aros del gizmo compone la rotación de verdad y la vuelve a pasar a H/P/B
  sin saltos de 180 a −180 durante el arrastre (antes sumaba el ángulo a un solo
  componente, correcto solo si los otros dos eran 0).
- Mover un hijo de un padre girado: la flecha y el movimiento ya coinciden (se usaba la
  rotación del padre en vez de su inversa).
- El rasterizador de miniaturas recorta los triángulos contra el plano cercano: un suelo
  que pasa por detrás de la cámara ya no desaparece entero.

**Limitaciones conocidas**: el render no tiene sombras, texturas ni la luz de la escena
(usa la iluminación de estudio de las miniaturas) y sale más claro que el visor.

**Probado**: build Debug sin avisos, ctest 2/2 (pruebas nuevas del render de cámara:
recorte del suelo y visibilidad de render), capturas de los ejes de objeto y de mundo sobre
el Cubo girado, del Visor de Imágenes con un render y de la Configuración de Render.
**Sin probar**: arrastrar con el ratón (ejes de objeto, aros, modo Eje), guardar el PNG
desde el diálogo y el backend OpenGL.

### 2026-11-07 — Todas las Vistas (F5), vistas Izquierda/Trasera/Inferior, rejilla ortográfica

**Añadido**
- **Todas las Vistas (F5)**: el visor se parte en cuatro paneles (Perspectiva, Superior,
  Derecha, Frontal), cada uno con su propia cámara. Un clic en un panel lo activa (marco
  claro): ahí van el gizmo, el clic sobre objetos, la selección por zona y la navegación.
  F5 otra vez, o F1–F4, maximiza ese panel conservando su encuadre. También en Paneles
  (Disposición > 4 Vistas, Todas las Vistas) y en el Commander.
- **Vistas Izquierda, Trasera e Inferior** en el menú Cámaras y en el Commander.
- **Rejilla en el plano de la vista**: en Frontal/Trasera la rejilla es XY y en
  Derecha/Izquierda es YZ, como en C4D (antes siempre era el suelo XZ y se veía de canto).

**Limitaciones conocidas**
- Con cuatro paneles no hay oclusión ambiental (SSAO): el G-buffer es de una sola cámara.
- El gizmo solo se dibuja en el panel activo.

**Probado**: build Debug sin avisos, ctest 2/2, capturas de Frontal, Izquierda y los cuatro
paneles (Vulkan). **Sin probar**: cambiar de panel con el ratón, el backend OpenGL con F5.

### 2026-11-06 (2) — Selección Rectangular y Lazo, vistas F1–F4, Conectar, HPB e instalador

**Añadido (funciona de verdad)**
- **Selección Rectangular (0)** y **Selección con Lazo (8)**: arrastrar en el visor dibuja la
  zona y al soltar se eligen los objetos que toca el rectángulo o que tienen algún punto
  dentro del lazo. Mayús añade, Ctrl quita. También en el menú Selección, el clic derecho
  del visor y el Commander. **9** vuelve a Selección en Vivo.
- **Vistas F1–F4**: Perspectiva, Superior, Derecha y Frontal (ortográficas). También en
  Cámaras y en Paneles del visor; el nombre de la vista sale arriba a la izquierda. En las
  ortográficas no se orbita (como en C4D); el paneo y el zoom sí. El clic sobre objetos
  funciona en todas.
- **Estado Actual a Objeto**: copia poligonal del objeto (y sus hijos) justo debajo del
  original, que no se toca.
- **Conectar Objetos** y **Conectar+Eliminar**: une la geometría de la selección (y sus
  hijos) en un solo Objeto Polígono con el eje y el material del primero.
- **Seleccionar Todo** (Ctrl+A) también en el menú Edición.
- Instalador: tareas para asociar **.iron** y **.lib4d** (doble clic abre la escena o el
  Navegador de Contenido) y **.c4d** (desmarcada por defecto, para no quitársela a Cinema
  4D). Todas aparecen además en "Abrir con". La URL del instalador pasa a staticforge.me.

**Cambiado**
- **Rotación en HPB de C4D**: el orden es primero Banco, luego Pitch y por último Heading,
  como en C4D, así los números H/P/B de un objeto girado en varios ejes coinciden con los
  de Cinema 4D. Las escenas `.iron` anteriores se convierten al abrirlas (misma
  orientación, otros números); el formato pasa a la versión 4.

**Probado**: build Debug sin avisos, ctest 2/2 (con pruebas nuevas del orden HPB, el bloqueo
de cardán y la conversión de escenas viejas), capturas de Conectar+Eliminar, Estado Actual,
vistas Superior y Frontal, y el instalador compila con Inno Setup 6.
**Sin probar**: arrastrar de verdad la selección rectangular y el lazo, F1–F4 pulsadas, el
instalador ejecutado (tocaría el registro) y comparar H/P/B con un objeto real girado en
los tres ejes dentro de Cinema 4D R19.
**Pendiente**: en Frontal y Derecha la rejilla se ve de canto (C4D la dibuja en el plano de
la vista); Todas las Vistas (F5); Izquierda, Trasera e Inferior.

### 2026-11-06 — Hacer Editable, menú del clic derecho en el visor y Commander

**Añadido (funciona de verdad)**
- **Hacer Editable (C)**: un Cubo, Esfera o Plano pasa a ser un Objeto Polígono con la misma
  malla que se ve. Conserva nombre, hijos, material y posición; actúa sobre toda la
  selección y se deshace con Ctrl+Z. Está en la tecla C, el botón de la barra izquierda, el
  menú Malla y el clic derecho del Gestor de Objetos (en gris si no hay nada convertible).
- **Clic derecho en el visor** (sin Alt y sin arrastrar): el menú del Modo Modelo de C4D
  R19 — Deshacer, Encuadrar, Hacer Editable, Agrupar, Seleccionar Hijos, Mover / Escalar /
  Rotar y Selección en Vivo. Lo que aún no existe sale en gris.
- **Commander (Mayús+C)**: buscador de órdenes por nombre (sin distinguir mayúsculas ni
  tildes); flechas para elegir, Enter para ejecutar, Esc para cerrar.
- Atajos nuevos: **S** encuadra la selección entera, **Espacio** vuelve a la herramienta
  anterior, **Ctrl+A** selecciona todos los objetos, **Ctrl+I** abre la información del
  proyecto.
- `--ui-cmd=make-editable|frame-selected|vp-menu|commander[:texto]` para verificar por
  captura sin clics.

**Arreglado**
- Las mallas generadas por Hacer Editable calculan su caja envolvente (sin ella el Tamaño
  salía 0 cm y fallaban el contorno, el clic y Encuadrar).

**Probado**: build Debug sin avisos, ctest 2/2, capturas de Hacer Editable (icono, Atributos,
Tamaño 200 cm, contorno), del menú del visor y del Commander filtrando "enc".
**Sin probar**: el clic derecho real con el ratón, las teclas pulsadas de verdad y Espacio.
**Pendiente**: vistas F1–F5 (no hay cámara ortográfica), Selección Rectangular (0) y Lazo
(8), L, W, Mayús+R, Ctrl+B, F9, Estado Actual a Objeto y Conectar.

### 2026-11-05 (4) — Menú contextual del Gestor de Objetos como el de C4D R19

**Cambiado**
- El clic derecho sobre un objeto del Gestor de Objetos tiene ahora los mismos grupos y el
  mismo orden que el de C4D R19: familias de etiquetas, Cargar Etiqueta Preestablecida /
  Restaurar Selección, capas, tomas, Seleccionar Hijos / Establecer como Raíz, Expandir /
  Contraer Todo, Hacer Editable y conexiones, Agrupar / Expandir Grupo / Eliminar Sin Hijos
  / XRef, pistas y curvas, información y ayuda. Lo que Iron3D todavía no tiene sale en gris.
  Las opciones propias de Iron3D (Añadir Hijo, Renombrar, Duplicar, Eliminar, Guardar como
  Preestablecido) siguen al final.
- La asignación de material pasa a **CINEMA 4D Etiquetas > Etiqueta de Textura**.

**Añadido (funciona de verdad)**
- **Agrupar Objetos** (también **Alt+G**): mete la selección en un Nulo nuevo, plegado, en
  el sitio del primero. Los objetos no se mueven en mundo.
- **Expandir Grupo de Objetos** (también **Mayús+G** con el ratón sobre el Gestor de
  Objetos; fuera de él Mayús+G sigue yendo al final de la animación) y **Eliminar Sin
  Hijos**: los hijos suben al sitio del objeto sin moverse y el objeto desaparece.
- **Seleccionar Hijos**, **Expandir Todo** y **Contraer Todo** (sobre la rama del objeto).
- **Información del Proyecto** abre el Proyecto en el Gestor de Atributos.

**Arreglado**
- Eliminar una selección que incluía un padre y uno de sus hijos podía usar memoria ya
  liberada; ahora solo se borran los de arriba.

**Probado**: compila sin avisos nuevos; tests (`c4d_geometry` con pruebas nuevas de
reparentado y de matriz a posición/rotación/escala, `preset_library`). Con el ratón en la
app: el menú completo, Agrupar, Expandir Grupo, Contraer Todo, Seleccionar Hijos y
Expandir Todo. **Sin probar con input**: los atajos Alt+G y Mayús+G (llaman a las mismas
funciones) y Eliminar Sin Hijos (mismo código que Expandir Grupo).

**Pendiente / conocido**
- Faltan las entradas de plugins de terceros del menú de C4D (Redshift, TrueSymmetry).
- El Gestor de Coordenadas rotula H/P/B sobre los ángulos X/Y/Z de Iron3D: con rotaciones
  en varios ejes no coinciden con los H/P/B que muestra C4D para el mismo objeto (la
  matriz exportada sí es la misma). Pendiente de arreglar.

### 2026-11-05 (3) — Exportar a `.c4d`, guardar objetos como preestablecidos y miniaturas

**Añadido**
- **Exportar a Cinema 4D** (con el Cineware SDK, solo Windows):
  - Archivo > Exportar > **Cinema 4D (*.c4d)...**: toda la escena con todos los materiales.
  - Archivo > **Guardar Objetos Seleccionados como...** (también en el menú Archivo del
    Gestor de Objetos): solo la selección y los materiales que usa.
  - Qué se exporta: jerarquía, nombres, posición/rotación/escala, visibilidad de visor y
    render. El Cubo, la Esfera y el Plano van como primitivas de C4D, editables allí. Las
    mallas van como Objeto Poligonal, con los vértices unidos y cuadriláteros donde se
    puede, y con etiqueta Phong si estaban suavizadas. Las luces van como Luz. Los
    materiales van como Material estándar: color más un reflejo GGX con la rugosidad
    (metal = Fresnel de conductor), asignados con etiqueta de textura.
- **Guardar como Preestablecido...** (clic derecho en un objeto, o Archivo en el Gestor de
  Objetos): exporta el objeto con sus hijos, le hace una miniatura y abre una ventana para
  elegir librería, carpeta y nombre. Si no hay ninguna librería, ofrece crear "Mis
  Preestablecidos".
- **Miniaturas de modelos**: un rasterizador propio por CPU, que no necesita la GPU, las
  renderiza desde arriba a la derecha y ajusta el encuadre al objeto. Se guardan en JPEG
  de 256×256 dentro de la `.lib4d`, como las de C4D. También salen al añadir un `.c4d`
  con "Añadir Archivos".
- Diálogo "Guardar como" del sistema (`saveFileDialog`).
- `c4d_probe --export escena.iron salida.c4d` exporta y vuelve a importar.
  `c4d_probe --preset escena.iron lib.lib4d Carpeta` guarda cada objeto como
  preestablecido. Flag `--save-preset` para abrir la ventana con el objeto seleccionado.

**Probado**
- **Cinema 4D R19, sin ventana, abre lo que exporta Iron3D**:
  - 3 `.c4d` exportados (materiales, primitivas y un personaje de 820 000 polígonos).
  - Una librería con 13 preestablecidos y sus miniaturas, escrita por Iron3D.
  - Al volver a leer lo que guardó R19, salen los mismos objetos, transformaciones,
    materiales y polígonos.
- Ida y vuelta Iron3D → `.c4d` → Iron3D: coinciden nombres, jerarquía, posiciones,
  rotaciones, escalas, visibilidad, colores, rugosidad y metal.
- Pruebas automáticas, en Windows y Linux:
  - Transformación inversa.
  - Malla a polígonos: el cubo vuelve con 8 puntos y 6 caras.
  - Miniatura.
  - Escribir y leer una miniatura JPEG en la `.lib4d`.
- Por captura: la ventana "Guardar como Preestablecido" con la miniatura de un grupo.

**Sin probar / limitaciones**
- Con el ratón no se ha probado: los menús nuevos, la ventana de preestablecido ni el
  diálogo "Guardar como".
- No se ha visto en la ventana de C4D si muestra las miniaturas de Iron3D. R19 sin
  ventana acepta la librería, pero eso no comprueba las miniaturas.
- El ID de aplicación que se escribe en los `.c4d` (1000001) es **provisional**. Hay que
  registrar uno de Static Forge en developers.maxon.net antes de publicar.
- Se pierden los triángulos degenerados (de área cero, invisibles). En un personaje
  fueron 72 de 820 000.
- Los materiales medio metálicos son una aproximación en C4D: un reflejo sin Fresnel con
  esa fuerza.
- Cada objeto se exporta con su transformación local. Si se guarda un hijo suelto, no se
  lleva la posición de sus padres.
- Las cámaras no se exportan, porque Iron3D aún no tiene cámaras como objeto.

### 2026-11-05 (2) — Crear librerías `.lib4d` y flecha de objeto fuera de vista

**Añadido**
- **Escritor propio de `.lib4d`**, sin código de Maxon. Escribe como C4D R19.024 y, como
  C4D, solo añade al final del archivo: nunca reescribe lo que ya hay.
- En el Navegador de Contenido:
  - Archivo > **Nueva Librería de Preestablecidos...** crea la librería en
    `library/browser`, así que es permanente.
  - Archivo > **Nueva Carpeta...** crea una carpeta en la librería o carpeta abierta.
  - Archivo > **Añadir Archivos a la Librería...**, también desde el clic derecho en la
    librería: los `.c4d` entran como modelos y el resto (texturas) tal cual.
- **Arrastrar desde el Explorador:** una `.lib4d` soltada en Iron3D se monta en el
  navegador. Otros archivos soltados encima del navegador se añaden a la librería
  abierta.
- Las texturas sin miniatura guardada enseñan la propia imagen.
- **Flecha de objeto fuera de vista**, como en C4D R19: si el objeto seleccionado queda
  fuera del visor, detrás de la cámara o demasiado lejos, sale una punta de flecha azul
  en el borde apuntando hacia él. Con un clic, la cámara viaja hasta él y lo encuadra.
- Flags de verificación: `--frame=<nombre>`, `--select=<nombre>` y `--travel`.

**Probado**
- **Cinema 4D R19, sin ventana, abre lo que escribe Iron3D.** Probado con una librería
  nueva (2 modelos, 1 textura y carpetas) y con una copia de una librería real de 19
  modelos a la que se le añadió uno: R19 carga los 20.
- Gramática de los registros: los 816 registros de 5 librerías reales se leen y se
  vuelven a escribir idénticos byte a byte.
- Prueba automática del escritor (crear, añadir, volver a leer, texto que no es UTF-8),
  en Windows y Linux.
- Por captura: la flecha en tres casos (a un lado, detrás de la cámara y diminuto) y el
  viaje de la cámara con `--travel`. El navegador lee las librerías escritas por Iron3D,
  con la miniatura de la textura.

**Sin probar / limitaciones**
- Con el ratón no se ha probado: el clic en la flecha, los diálogos de nombre ni
  arrastrar desde el Explorador.
- Todavía no se puede **guardar un objeto de la escena** como preestablecido, porque
  Iron3D aún no exporta a `.c4d`. Los modelos añadidos no llevan miniatura.
- No se puede borrar, renombrar ni mover dentro de una librería.
- La hora de creación que se guarda no está confirmada; la fecha sí.

### 2026-11-05 — Navegador de Contenido y librerías `.lib4d`

**Añadido**
- **Lector propio de librerías de preestablecidos de Cinema 4D (`.lib4d`)**, sin código ni
  SDK de Maxon: carpetas, modelos, texturas y miniaturas. Funciona también en Linux.
- **Navegador de Contenido** en el hueco del Gestor de Objetos, como en C4D R19: pestaña
  vertical "Navegador de Contenido", Ventana > Navegador de Contenido o Mayús+F8
  (Mayús+F1 vuelve a Objetos).
  - Árbol: Catálogos, Elementos Recientes, Favoritos, Ordenador, Preestablecidos, Red y
    Resultado de la Búsqueda. Funcionan Preestablecidos, Elementos Recientes y Búsqueda;
    el resto sale desactivado.
  - Rejilla con la miniatura y el nombre de cada elemento, en tres tamaños (menú Ver).
  - Atrás, Adelante, Subir y búsqueda por nombre en todas las librerías.
  - Cargar un modelo: doble clic, Intro, clic derecho > Cargar en la Escena, o arrastrarlo
    al visor. Se añade a la escena actual y se puede deshacer.
- **Librerías temporales y permanentes.** Una `.lib4d` abierta con Archivo > Abrir (o
  `Iron3DStudio.exe librería.lib4d`) se monta en Preestablecidos solo para esta sesión y
  abre el navegador. Las que están en `library/browser/`, junto al ejecutable, salen
  siempre. "Hacer Permanente" copia una temporal a esa carpeta.
- Flags de verificación: `--load-preset=<ruta>` (con un `.lib4d`) carga ese modelo al
  arrancar, y `--no-focus` abre la ventana sin quitar el foco.

**Probado**
- Prueba automática con una librería sintética (CI, Windows y Linux).
- Con 6 librerías reales (Crying Child, FNaF 1 TCG, un pack de 37 luces, FNaF 4 con 30
  modelos, Kirk, DONNIES DEV): todos los modelos se descomprimen a un `.c4d` válido y las
  miniaturas se decodifican.
- Por captura: abrir Iron3D con un `.lib4d` monta la librería y enseña el navegador con
  las miniaturas. Con `--load-preset`, el modelo de Crying Child entra en la escena con su
  jerarquía, sus articulaciones y sus 26 materiales.

**Sin probar / limitaciones**
- Lo interactivo (doble clic, arrastrar al visor, menús del navegador, búsqueda, Hacer
  Permanente) no se ha probado con ratón: se probó solo por captura.
- El encuadre de la cámara tras cargar un preestablecido no está confirmado.
- Las texturas de la librería se ven en el navegador, pero los materiales de Iron3D
  todavía no cargan texturas: el modelo entra con sus colores.
- Todavía no se pueden **crear** librerías (Nueva Librería de Preestablecidos, guardar tus
  propios preestablecidos): hace falta escribir `.lib4d`, y eso viene después.
- Catálogos, Favoritos, Ordenador y Red están en el árbol pero aún no hacen nada.

### 2026-11-05 — Avisos del importador para rigging

- Las articulaciones (joints) y el deformador Piel tienen su propio aviso al importar.
  Antes salían como "sin geometría guardada" y recomendaban una opción de guardado que no
  tiene nada que ver. Siguen entrando como Nulo.
- **Probado** con `c4d_probe` sobre modelos con rigging sacados de bibliotecas `.lib4d`
  (166 objetos, 101 articulaciones y 14 deformadores Piel).

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
