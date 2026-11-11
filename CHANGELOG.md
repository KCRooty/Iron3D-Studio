# Cambios de Iron3D Studio

Todavía no hay versiones numeradas: Iron3D está en desarrollo temprano y nada de esto es
una versión terminada. Cada bloque lleva la fecha en que entró, qué se probó y qué no.
Las notas largas, en tono de blog, están en [`docs/devlog/`](docs/devlog/).

## Sin publicar

### 2026-11-11 (16) — Ajustes completos de material, shaders de C4D y texturas de cualquier .lib4d

**Nuevo**
- **Texturas de cualquier `.lib4d`**: las rutas `preset://<librería>.lib4d/tex/...` (o solo el
  nombre del archivo) se buscan primero en la librería que nombra la ruta, luego en la del
  modelo y luego en todas las montadas. Cada librería se extrae en su propia carpeta de
  caché, así que dos packs con un `Skin.png` cada uno ya no se pisan. Lo que no está en
  ninguna sale en el informe de importación.
- **Ajustes de cada canal como en el editor de R19**: Color y Brillo por separado, Textura,
  Muestreo, Desenfoque, Modo de Mezcla (Normal/Añadir/Restar/Multiplicar) y Fuerza de
  Mezcla en Color, Luminancia y Transparencia; Modelo (Lambertiano/Oren-Nayar), Caída
  Difusa, Nivel Difuso y Rugosidad; Relieve, Normal y Alfa con sus opciones; casillas de
  canal del Básico. Capas de Reflectancia completas: Tipo, Atenuación, Rugosidad, Fuerza
  Reflexión/Especular/Relieve, Color de Capa (color, brillo, mezcla), Fresnel e IOR; Brillo
  Reflexión/Especular Global y Pase Separado. Se importan del `.c4d`/`.lib4d`, se editan en
  el Gestor de Atributos y en el Editor de Material, se guardan en el `.iron` (formato 8) y
  el exportador escribe los que C4D acepta.
- **Shaders de C4D en el visor**: el árbol de shaders de cada canal se importa entero (Imagen,
  Color, Filtro, Colorizador, Capa con sus modos de fusión y capas de efecto, Degradado) y se
  hornea a una imagen junto con el color, el brillo y la mezcla del canal. Antes se cogía la
  primera imagen de dentro y se ignoraba el resto: el Filtro que tiñe los ojos de Afton o
  el Capa que mezcla dos texturas en los trajes de FNaF 1 TCG ahora se ven. Funciona igual en
  Vulkan y OpenGL. El Editor de Material enseña el árbol.
- `c4d_probe --audit <librerías>`: carga cada modelo por el mismo camino que la UI y cuenta
  texturas perdidas, avisos y shaders. Con las 30 librerías de prueba: 248 modelos, 0 fallos,
  2624 texturas, 1 perdida (una ruta a un juego Source que no está en ninguna librería).

**Arreglado**
- Abrir un modelo de una `.lib4d` desde la línea de comandos dejaba el Cubo de la escena
  inicial debajo del modelo.
- `--also=` no decía nada si el elemento no existía.

**Probado**
- Las 3 pruebas (`iron3d_tests`, `c4d_roundtrip_test`, `preset_library_test`) pasan.
- Auditoría de las 30 librerías (arriba). Unidades del Filtro comprobadas con modelos reales
  (el tono sale en grados razonables: 185°, 195°, 214°). Orden del shader Capa comprobado
  con modelos reales: la lista va de arriba a abajo (una capa Transformar en la posición 0
  solo tiene sentido encima de la imagen).
- Freddy Fazbear [VF_2.0] (FNaF 1 TCG) + FNaF World Lighting capturado en Vulkan y OpenGL:
  carga con sus 3 luces infinitas, sin el Cubo, y las dos API se ven igual.

**Sin probar / bugs conocidos**
- **No se ve todavía como en R19.** El naranja de los trajes de FNaF 1 TCG sale de 3 capas
  de Reflectancia especulares de color con Atenuación Añadir, y el visor aún no pinta cada
  capa por separado (solo saca de ellas un metal/rugosidad). Es lo siguiente.
- **Pose de Freddy VF_2.0 distinta a R19**: en R19 está en T-pose; en Iron3D brazos y cabeza
  salen girados. Fallo del rig/Piel, sin investigar todavía.
- Ruido, Fresnel, Normalizador y el resto de procedurales no se evalúan (Normalizador y
  similares dejan pasar la imagen de dentro; un procedural suelto no tapa el color). Las
  carpetas del shader Capa y las capas Distorsión/Transformar tampoco.
- La matemática de Filtro, Colorizador y modos de fusión es propia y aproximada; no está
  comparada píxel a píxel con un render de R19.
- Muestreo, Desenfoque, Modelo difuso, Atenuación y Fresnel se guardan y editan pero el
  visor aún no los usa.
- Las texturas horneadas no se liberan al cambiar un ajuste (se crea una nueva por valor
  aplicado; los campos que hornean solo aplican al soltar el control).
- El exportador escribe la imagen de dentro de los shaders, no el árbol.
- La interfaz nueva (pestañas de material, Editor de Material) compila pero no se ha
  probado a mano.

### 2026-11-11 (15) — Luces de la escena en el visor y materiales por selección

**Nuevo**
- **Las luces de la escena iluminan el visor** (Vulkan y OpenGL), hasta 32 a la vez: color,
  intensidad, tipo (Omni, Foco con su cono interior/exterior, Infinita/Paralela) y
  decaimiento (Ninguno, Cuadrado Inverso, Lineal, Paso, Ajustado Cuadrado Inverso…).
  También respetan No Ilumina, Difusión y Especular. Antes había una sola luz blanca en la
  posición de la primera luz, y daba igual cuántas se añadieran o cómo se configuraran.
  Sin luces en la escena, la luz por defecto sigue en la cámara. La sombra la proyecta la
  primera luz que tenga sombra activada (en OpenGL, todavía sin sombras).
- La Luminancia del material se suma en el visor de Vulkan.
- **Atributos de la luz editables**, como en R19: pestaña General (Color, Intensidad, Tipo,
  Sombra, No Ilumina, Difusión, Especular), Detalles (ángulos del foco, Decaimiento, Radio
  Interior, Radio/Decaimiento, tamaño del área) y Sombra. Todo con deshacer.
- Crear > Luz: Foco, Luz Infinita y Luz de Área ya crean su tipo.
- **Materiales por Selección de Polígonos**: las etiquetas de textura restringidas a una
  selección pintan solo esos polígonos, y la de más a la derecha manda donde se pisan, como
  en C4D. Antes se aplicaban a todo el objeto: el traje de William Afton salía blanco
  entero por la etiqueta del cuello de la camisa, y la cabeza y los ojos de un solo color.
  Se guardan en el `.iron`.

**Arreglado**
- **Luces y cámaras importadas de `.c4d` salían con los valores por defecto** (toda luz era
  una Omni blanca al 100 %). C4D guarda sus parámetros en el objeto, no en su contenedor, y
  el importador leía el contenedor. Ahora se leen bien: color, intensidad, tipo, sombra,
  decaimiento, focal, sensor, zoom.
- **El exportador escribía las luces y cámaras en el mismo sitio equivocado**, así que un
  `.c4d` exportado por Iron3D se abría en C4D con las luces por defecto. El test de ida y
  vuelta no lo veía porque leía del mismo sitio; ahora lee y escribe por el camino bueno y
  pasa.

**Probado**
- Packs de luces reales (`freddy fazbear lighting pack`, `fnaf lighting pack`): el volcado
  de la escena coincide con los valores que muestra R19 (p. ej. FNaF World Lighting: tres
  Infinitas, azul-violeta 100 %, azul 150 %, blanca 24 %). Capturas en Vulkan y OpenGL de
  William Afton con FNaF World Lighting y Stage Lighting 2.
- Partes de material de Afton comprobadas con `c4d_probe` (cabeza, ojos, traje, zapatos,
  dientes, cinturón): los rangos suman exactamente los triángulos de cada malla.
- `iron3d_tests`, `c4d_roundtrip_test` y `preset_library_test` pasan.

**Sin probar / pendiente**
- No se ha abierto en R19 un `.c4d` exportado tras el arreglo del exportador.
- Al exportar, las partes por selección se pierden (solo va el material del objeto; avisa).
- Si se edita la malla con las herramientas de modelado, las partes pueden quedar
  desalineadas (si ya no caben, el objeto entero vuelve a su material).
- Las miniaturas del Navegador de Contenido todavía no usan las partes.
- Tubo, Área e IES iluminan como una Omni; Paralela y Foco Paralelo como una Infinita.
- Pestañas de la luz sin hacer: Visibilidad, Fotométrico, Cáusticas, Ruido, Lentes,
  Proyecto (son de render).
- `[C4D] [FNaF 1] lights pack update by FazbearRenders.lib4d` no se abre ("No se puede
  leer el archivo"), sin investigar.
- Herramientas de verificación: `preset_library_test --list/--extract`, Iron3D `--also=` y
  `IRON3D_DUMP_LIGHTS=1` (parámetros en bruto de cada luz al importar).

### 2026-11-10 (14) — Texturas en el visor y personajes con Piel

**Nuevo**
- **Texturas en el visor** (Vulkan y OpenGL): la textura del canal Color del material se
  ve en el modelo, con el mismo mapeo que las miniaturas. Desplazamiento y longitud de la
  etiqueta de textura, mosaico activado o no (sin mosaico, fuera de 0..1 queda el color
  base). Las texturas se suben con mipmaps (sRGB en Vulkan) y se guardan en caché. Si una
  textura falta o tiene un formato que no se lee, la consola lo dice una sola vez y se usa
  el color base.
- Se leen también BMP, TGA y PSD (además de PNG y JPG).
- **Piel (Skin) de C4D al importar**: las mallas con etiqueta de pesos y un deformador Piel
  activo se importan con la Piel aplicada, en la pose de las articulaciones guardada en el
  archivo. Antes, en un personaje de `.lib4d`, la cabeza salía 100 veces más grande y a
  cientos de unidades del cuerpo. El informe de importación lo avisa.
- Opciones de depuración: `--dump-scene` (lista cada objeto con su caja en el mundo, su
  material y si su textura está en disco) y `--ui-cmd=close-report` (cierra el informe de
  importación).

**Arreglado**
- Los modelos grandes desaparecían del visor: el plano lejano estaba fijo en 100 y la
  distancia de cámara limitada a 0,5–50. Ahora el recorte va con la distancia (0,02 a
  100 000) y la rueda y el dolly hacen zoom proporcional.
- Encuadrar (H, Alt+H y al cargar) contaba los objetos ocultos. Los objetivos de un Pose
  Morph dejaban al personaje como un punto. Ahora solo cuenta lo visible; si no hay nada
  visible, lo cuenta todo.
- Si la GPU no admite 256 bytes de push constants, Iron3D usa OpenGL en vez de cerrarse.

**Probado**
- Tests (`c4d_geometry`, `c4d_roundtrip`, `preset_library`) en verde.
- `William Afton.lib4d` en Vulkan y en OpenGL, por captura: el personaje entero, encuadrado,
  cabeza y pelo en su sitio, texturas de piel (cara y manos) y de cuero (zapatos).
  Los pesos de las 23 mallas con Piel suman 1 en todos los puntos (comprobado con una
  traza temporal).

**Sin probar / no hecho**
- Piel animada (solo la pose guardada), morphs del Pose Morph, Piel de tipo esférico o
  con mezcla, y deformadores Piel que no son hijos directos de la malla.
- Mapas que no son de Color (normal, relieve, especular...), EXR/HDR, proyecciones que no
  son UVW, texturas dentro de shaders de capa o procedurales en el visor.
- Carga asíncrona: las texturas se suben al dibujarse por primera vez (pausa breve). No se
  liberan hasta cerrar Iron3D.
- Otros `.lib4d` de `MODELOS 3D` aparte de Afton.

### 2026-11-09 (13) — El instalador funciona en otros PCs

**Arreglado**
- El instalador no llevaba el runtime de Visual C++: en un PC sin el «Microsoft Visual C++
  Redistributable» Iron3D no arrancaba (faltaba `VCRUNTIME140_1.dll`). Ahora
  `msvcp140.dll`, `vcruntime140.dll` y `vcruntime140_1.dll` van junto al ejecutable.
- Sin drivers Vulkan, el ejecutable no llegaba ni a abrirse, aunque se eligiera OpenGL.
  Si los drivers no servían, se cerraba sin avisar. Ahora `vulkan-1.dll` se carga en
  diferido y, si Vulkan no sirve en ese equipo, Iron3D usa OpenGL para esa sesión sin tocar
  las preferencias. Si tampoco hay OpenGL 3.3, sale un aviso en vez de cerrarse en
  silencio.

**Probado**
- La build Release arranca con Vulkan y con OpenGL en el PC de desarrollo.
- `dumpbin` confirma que `vulkan-1.dll` se carga en diferido.

**Sin probar**
- Un PC sin Vulkan de verdad, y un PC sin el runtime de Visual C++ instalado.
- El instalador no está firmado: Windows SmartScreen avisa al abrirlo («Más información →
  Ejecutar de todas formas»).

### 2026-11-09 (12) — Librerías .lib4d e importación/exportación exacta

**Librerías .lib4d (cerrado lo pendiente)**
- **Renombrar, Mover a y Eliminar** en el menú contextual de cualquier elemento o carpeta
  del Navegador de Contenido. Las carpetas se llevan su contenido; las carpetas de destino
  que falten se crean. Eliminar pide confirmación.
- Se edita como lo hace C4D: el bloque viejo pasa a `ivld` en su sitio (4 bytes) y el
  registro nuevo, con el mismo id, se añade al final. No se reescriben cientos de MB.
- El lector usa ahora la gramática real de los registros (contenedores con tipos entero,
  entero de 64 bits, cadena y contenedor) en vez de buscar cadenas por orden. Ya no
  depende de que el bloque de datos vaya antes del nombre. Las posiciones de datos y
  miniaturas salen de sus claves (14 y 5). Si un registro no encaja, se usa el método
  anterior.
- **Url raíz**: la librería guarda dentro `preset://<archivo>.lib4d` y C4D no la monta si
  no coincide con el nombre del archivo. Al abrir una renombrada fuera aparece un aviso, y
  el menú de la librería ofrece **Corregir Url Interna**: deja una copia `.bak` y escribe
  una limpia. **Hacer Permanente** también la corrige al copiar.
- Escritor nuevo `rewritePresetLibrary`: copia limpia sin bloques anulados, con otra url
  raíz y otro título.

**Probado**
- Tests: los 181 registros de dos librerías reales (`bh2_ffd`, `Crying Child`) se leen y se
  reescriben idénticos byte a byte. Renombrar, mover (con contenido y carpetas
  intermedias), borrar y reescribir sobre una librería sintética. Los datos y las
  miniaturas no cambian.
- Cinema 4D R19 sin ventana (`Commandline.exe -nogui -iron3d_lib4d`, el extractor como
  juez): en una librería escrita por Iron3D, renombrada, movida, con una carpeta renombrada
  y elementos borrados, R19 ve exactamente lo esperado y extrae los modelos y la textura
  (idéntica byte a byte). En su copia reescrita con otro nombre ve lo mismo, con la url
  nueva.

**Sin probar**: los diálogos nuevos con el ratón de verdad, y editar dentro del C4D con
interfaz una librería editada por Iron3D.

**Importar y exportar .c4d / .iron con todos los datos**
- **Materiales**: Color, Luminancia, Transparencia (con índice de refracción), Normal,
  Relieve y Alfa, cada uno con su textura. Las capas de Reflectancia de C4D se guardan tal
  cual (tipo, Fresnel, fuerza, especular, rugosidad, IOR, color y sus texturas) y vuelven
  igual al exportar. Si el material se edita en Iron3D, se exporta una capa nueva hecha
  desde metal/rugosidad.
- **Texturas**: rutas relativas o absolutas. Se buscan junto al .c4d, en `tex/` y en
  `../tex/`. Las de un preestablecido se sacan de la librería a una carpeta temporal.
  El "./" que añade C4D al guardar se quita.
- **Etiquetas de textura**: proyección, desplazamiento, longitud, mosaico, sin costuras y
  lado. Solo se escribe una etiqueta cuando cambia algo respecto al padre.
- **UV**: una UV por esquina (etiqueta UVW). Donde hay una costura de UV no se junta en
  cuadrilátero.
- **Luces**: los 10 tipos de C4D, color, intensidad, ángulos interior y exterior, sombra,
  decaimiento y radios, forma y tamaño del área.
- **Cámaras** (objeto nuevo en Iron3D): focal, sensor, zoom, perspectiva u ortográfica.
  La etiqueta Objetivo va y vuelve como ruta de nombres.
- **Transformación**: posición, HPB y escala se escriben tal cual (SetRelPos/Rot/Scale),
  sin pasar por una matriz.
- También van jerarquía, nombres y visibilidad.
- **Animación**: pistas de Posición, Rotación y Escala con los fotogramas por segundo del
  documento. Las tangentes se escriben a mano para que C4D dibuje la misma curva que
  Iron3D.
  La interpolación por clave (Spline, Lineal o Escalón) va y vuelve: el exportador la
  escribe en cada clave de C4D y el importador la lee. Probado con R19 sin ventana: una
  escena con tramos Lineal y Escalón se abre, se vuelve a guardar y reimporta igual.
- **.iron formato 7**: números con todos sus dígitos, cadenas escapadas (comillas, `%` y
  saltos de línea), UV en las mallas, luces, cámaras, etiquetas de textura y los canales y
  capas nuevos. Los .iron 6 se siguen leyendo.
- **Render por CPU** (miniaturas y Visor de Imágenes): textura de Color con UV en
  perspectiva correcta, muestreo bilineal, mosaico y desplazamiento de la etiqueta.
  También suma la Luminancia.
- Arreglo: al importar mallas se perdían todas las UV (`PolySoup::append` con una sopa
  vacía).

**Probado**
- `c4d_roundtrip_test` (nuevo, en ctest): exporta una escena con todo lo anterior (3
  materiales con 6 texturas y 2 capas, 4 luces, 2 cámaras con objetivo, grupo oculto,
  malla con UV y cubo animado), la reimporta y compara los números con tolerancia 1e-4.
  Todo coincide. Las curvas de C4D (evaluadas por Cineware) se separan de las de Iron3D
  como mucho 3·10⁻⁶ en posición y escala y 4·10⁻⁴ grados en rotación, en los fotogramas
  −5 a 50.
- **Cinema 4D R19 sin ventana**: el .c4d exportado se mete en una .lib4d, R19 lo abre y lo
  vuelve a guardar (`Commandline.exe -nogui -iron3d_lib4d`, LoadDocument + SaveDocument).
  `c4d_roundtrip_test --compare` sobre lo que guardó R19 da los mismos números: materiales,
  capas, texturas encontradas, luces, cámaras, HPB, visibilidad, claves y curvas.
- `iron3d_tests`:
  - ida y vuelta por .iron 7 exacta, y guardar lo leído da el mismo texto;
  - lectura de un .iron 6;
  - UV Iron3D → C4D → Iron3D;
  - capas ↔ PBR;
  - FOV de la cámara;
  - textura en el render por CPU (izquierda/derecha, mosaico, archivo que falta).
- Importar los 8 .c4d de prueba guardados por R19: sin errores.

**Sin probar / limitaciones**
- **Visor (Vulkan)**: todavía no dibuja texturas. Solo el render por CPU lo hace, y solo
  con el canal Color. Normal, Relieve, Alfa y Transparencia se guardan y se exportan, pero
  no se dibujan.
- **Proyecciones distintas de UVW** (Esférica, Cúbica...): se conservan al ir y volver, pero
  Iron3D dibuja con las UV.
- **Tangentes automáticas de C4D**: al importar animaciones de C4D, las claves llegan
  exactas, pero entre claves Iron3D usa su propia regla. En la prueba la diferencia era de
  hasta ~5 % del recorrido. Las claves exportadas van sin "Auto", con las tangentes
  explícitas.
- Los objetos con transformación congelada se importan por su matriz, con aviso.
- Los shaders procedurales (Ruido, Degradado...) no se importan. De Capa o Filtro se toma
  la imagen que llevan dentro.
- La etiqueta Objetivo solo funciona con un objetivo dentro de lo exportado.
- Sin probar en el C4D con interfaz:
  - si el orden de las capas que ve el usuario es el esperado (Iron3D usa el orden de
    `GetReflectionLayerIndex`);
  - cómo se ven las luces y cámaras exportadas.
- Ni el Gestor de Atributos ni el visor tienen todavía controles para editar luces,
  cámaras ni los canales nuevos.

### 2026-11-09 (11) — Selección por topología y Soldar, Cerrar Agujero, Subdividir e Invertir Normales

**Añadido** (activo en los modos Puntos, Aristas y Polígonos con un objeto editable):
- **Menú Selección**:
  - **Selección de Bucle** (modo Aristas): sigue cada arista seleccionada por los puntos de
    cuatro aristas hasta cerrar la vuelta o llegar a un polo. Si la arista es de borde,
    sigue el borde abierto.
  - **Selección de Anillo** (modo Aristas): de cuadrilátero en cuadrilátero, por la arista
    opuesta.
  - **Aumentar**, **Reducir**, **Invertir** y **Seleccionar Conectados**: en los tres
    modos. Los vecinos son los puntos unidos por una arista, y las aristas o polígonos que
    comparten un punto.
- **Menú Malla**:
  - **Cerrar Agujero Poligonal**: crea un polígono por cada borde abierto que toque la
    selección; sin selección, cierra todos.
  - **Soldar**: junta los puntos seleccionados en su centro. Las caras que se quedan con
    menos de 3 puntos desaparecen.
  - **Subdividir**: lineal, sin suavizar. Parte cada polígono de n lados en n
    cuadriláteros por los puntos medios y el centro. Los vecinos sin seleccionar reciben
    el punto medio, así que no quedan puntos en T.
  - **Invertir Normales**.
  - Subdividir e Invertir Normales actúan sobre los polígonos seleccionados o, si no hay
    ninguno, sobre toda la malla.
- Las operaciones de malla se pueden deshacer, con un paso cada una. Las de selección no
  crean paso de deshacer, igual que el resto de la selección de componentes.
- Núcleo en `src/core/MeshEdit.*`. Para probarlo: `--ui-cmd=meshop=loop|ring|grow|shrink|connected|invert|weld|close|subdiv|flip`.
- Análisis de lo que falta respecto a las páginas de modelado y MoGraph de C4D, con coste
  estimado, en la nota del proyecto en el vault.

**Probado**: compila sin avisos y pasan los tests nuevos:
- en el cubo, el anillo da 4 aristas y el bucle no avanza (sus puntos tienen 3 aristas);
- en la esfera, el bucle del ecuador da 18 aristas y el anillo vertical también 18;
- aumentar, reducir y conectados sobre la cara de arriba del cubo;
- soldar la cara de arriba deja una pirámide de 5 puntos y 5 polígonos, cerrada;
- cerrar el agujero del cubo abierto da 6 polígonos, con la tapa nueva hacia arriba y
  solo si el borde toca la selección;
- subdividir la cara de arriba da 13 puntos y 9 polígonos, cerrada; subdividir todo da
  26 puntos y 24 polígonos;
- invertir normales.

Verificado por captura limpia, con `--ui-cmd`, sobre el Cubo hecho editable:
- Subdividir una cara da «Polígonos: 4 de 9»;
- borrar una cara y Cerrar Agujero da «1 de 6»;
- Soldar 2 puntos da «Puntos: 0 de 7».

La captura de Selección de Bucle en la esfera se invalidó dos veces porque hubo entrada del
usuario mientras corría; esa parte solo la cubren los tests.

**Sin probar**: los menús con el ratón y OpenGL.

**Limitaciones**:
- Bucle y Anillo no funcionan en modo Polígonos ni de forma interactiva al pasar el
  ratón: amplían la selección de aristas que ya hay.
- Los acordes U~L, U~B, U~Y, U~K, U~I, U~W, U~S, U~R, M~D y M~Q se ven en el menú pero
  todavía no funcionan con el teclado.
- Cerrar Agujero triangula en abanico: un agujero cóncavo puede salir con triángulos
  cruzados.
- Subdividir no suaviza (no es Subdivisión de Superficie).
- Soldar no tiene la opción de soldar al punto activo, solo al centro.
- Siguen sin hacerse Puente, Puntada y Cosido, y MoGraph: ver la nota de huecos.

### 2026-11-07 (10) — Edición de claves en la Línea de Tiempo y Editor de Curvas

**Añadido**:
- **Claves en la Línea de Tiempo**:
  - Clic en una clave la selecciona y Mayús+clic añade o quita.
  - Arrastrarlas en horizontal las mueve, con vista previa y en fotogramas enteros.
  - Supr las borra cuando el ratón está sobre la Línea de Tiempo o el Editor de Curvas.
  - Al moverlas se llevan las tres pistas (Posición, Rotación y Escala) de ese fotograma.
    Si caen encima de otra clave, la sustituyen.
- **Interpolación por clave**: Spline (la de siempre), Lineal o Escalón, para el tramo que
  empieza en esa clave. Se cambia desde Animación > Claves Seleccionadas, con el clic
  derecho o con los botones del Editor de Curvas.
- **Editor de Curvas** (Animación > Editor de Curvas...):
  - Muestra las pistas del objeto activo como valor por fotograma: posición en cm,
    rotación H/P/B en grados y escala, con una casilla por canal.
  - Las claves se seleccionan con clic y llevan marcada su interpolación (L/E).
- Todo se puede deshacer, un paso por operación.
- **.iron v6**: cada clave lleva un `i=l` o `i=s` opcional (Spline no escribe nada). Los
  archivos v5 se siguen leyendo igual y las claves quedan como Spline.

**Probado**: compila sin avisos y pasan los tests nuevos:
- tramo Lineal: el punto medio exacto;
- tramo Escalón: se mantiene hasta la clave siguiente;
- mover conserva la interpolación;
- mover encima de otra clave la sustituye;
- borrar;
- ida y vuelta por .iron con `i=`.

Verificado por captura limpia, con una escena de prueba temporal (no la de ejemplo) y
`--ui-cmd`:
- el Editor de Curvas dibuja el tramo Lineal recto, el Escalón plano con el salto en la
  clave siguiente y el Spline curvo;
- `keys-move=10` lleva la clave 30 a 40 sin perder el Escalón, y la marca de la Línea de
  Tiempo se mueve con ella.

**Sin probar**: el clic y el arrastre reales con el ratón (en la Línea de Tiempo y en el
Editor de Curvas), Supr, el menú del clic derecho y OpenGL.

**Limitaciones**:
- En el Editor de Curvas las claves no se arrastran: ni en vertical (valor) ni en
  horizontal. Para moverlas hay que usar la Línea de Tiempo.
- No hay tangentes editables: Spline es siempre la curva suave automática.
- La interpolación se elige por fotograma, para todas las pistas a la vez, no por canal.
- El botón Selección de Claves de la barra sigue sin hacer nada.

### 2026-11-07 (9) — Arreglo: seleccionar con Mover, Rotar y Escalar

**Corregido**: en los modos Puntos, Aristas y Polígonos solo se podía seleccionar con la
herramienta de selección. Con Mover, Rotar o Escalar, cualquier clic fuera de las flechas
arrancaba un arrastre libre del gizmo y nunca llegaba a elegir componentes. En modo Modelo
pasaba lo mismo con los objetos: con uno seleccionado, un clic en otro no lo elegía.

Ahora funciona como en C4D:
- Un clic en un handle de verdad (flecha o aro X/Y/Z, o el aro exterior de Rotar) arrastra
  ese eje, igual que antes.
- Fuera de los handles:
  - Un componente u objeto sin seleccionar se elige (Mayús añade, Ctrl quita) y, si se
    arrastra, se mueve directamente.
  - Uno que ya estaba seleccionado arrastra la selección tal cual.
  - En vacío, arrastrar sigue moviendo la selección en modo libre; un clic sin arrastre
    (menos de 3 px) lo deselecciona todo, salvo con Mayús.
- Hasta pasar de 3 px no se mueve nada ni se crea paso de deshacer, así que un clic de
  selección ya no deja un «Deshacer» vacío.
- `TransformGizmo::pickHandle` dice si el cursor está sobre un handle de verdad, y
  `beginDrag` admite forzar el modo Free.

**Probado**: compila sin avisos y pasan los tests nuevos del gizmo:
- lejos de las flechas no hay handle, aunque `beginDrag` lo siga aceptando como Free;
- la flecha X sí es handle;
- en Rotar, el centro no es handle y el aro Z y el aro de vista sí;
- se puede arrancar un arrastre Free forzado.

Verificado por captura limpia (Cubo editable, modo Polígonos con Mover, 2 de 6 caras
seleccionadas): el gizmo y el resaltado se dibujan igual que antes.

**Sin probar**: el clic real con el ratón en el visor, porque las capturas no generan
entrada sintética. Falta comprobarlo a mano en los tres modos de componentes y en modo
Modelo, con Mover, Rotar y Escalar.

**Limitaciones**:
- Con Ctrl el clic solo quita y no arrastra.
- Mayús+clic sobre un objeto ya seleccionado lo arrastra en vez de quitarlo de la
  selección (para quitarlo, Ctrl).
- El resaltado al pasar el ratón sigue marcando el modo libre fuera de las flechas.

### 2026-11-07 (8) — Herramientas de modelado: Extrusión, Extrusión Interna, Bisel y Cuchillo

**Añadido** (menú Malla y atajos; con una herramienta activa no hay gizmo):
- **Extrusión (D)**: separa los polígonos seleccionados como una región y los desplaza por
  la normal media de cada punto. En el borde de la región salen paredes; entre caras
  vecinas seleccionadas no. Al terminar queda seleccionada la tapa.
- **Extrusión Interna (I)**: encoge cada polígono seleccionado hacia su centro y crea un
  anillo de cuadriláteros.
- **Bisel (M~S)**, en modo Polígonos: lo mismo con Desplazamiento de Inserción y
  Extrusión. Es el «bisel de polígonos» de C4D sin redondeo ni subdivisiones.
- **Cuchillo (K)**, modo Línea: arrastrar dibuja la línea y al soltar corta por el plano
  de la cámara que pasa por ella. Si hay polígonos seleccionados, corta solo esos; si
  no, todos los que cruza, o solo los visibles con «Solo Visibles» (activa por defecto).
  Solo corta un polígono si los dos puntos de corte caen dentro del tramo dibujado.
- Extrusión y Bisel se usan arrastrando en horizontal en el visor, que cambia la
  cantidad con vista previa, o con el botón Aplicar de la ventana de la herramienta.
  Todas pasan al modo Polígonos y se pueden deshacer: un paso por arrastre o corte.
- Núcleo en `src/core/MeshEdit.*`, con tests. Las mallas resultantes guardan sus
  polígonos (`FACES`), así que un corte no se vuelve a juntar.
- Esc suelta la herramienta; E/R/T vuelven a Mover/Rotar/Escalar.

**Probado**: compila sin avisos y pasan los tests:
- extruir una cara del cubo: 12/20/10 y cerrada;
- extruir una región de 2 caras: sin pared interior;
- biselar: 10 polígonos, con el interior arriba;
- cuchillo por x = 0: 4 caras, 20 aristas y cerrada; también solo con los polígonos
  permitidos y solo dentro del tramo.

Verificado por captura limpia, sin input del usuario y con `--ui-cmd`:
- extruir 100 cm: «1 de 10», con la caja alargada;
- biselar 30/40: «1 de 10»;
- cuchillo con una línea horizontal: el corte se ve en las caras visibles (19 aristas).

**Sin probar**: el arrastre en el visor para fijar la cantidad, la línea del Cuchillo
dibujada con el ratón, los atajos D/I/M~S/K y OpenGL.

**Limitaciones conocidas**:
- Bisel y Extrusión Interna van por polígono: no hay «Conservar Grupos» para insertar
  una región entera.
- No hay bisel de aristas ni de puntos.
- Al Cuchillo le faltan los modos Bucle, Plano y Polígono.
- Si un polígono vecino no entra en el corte (fuera del tramo o tapado), queda un punto
  en T en su arista.
- La cantidad que se arrastra no tiene en cuenta la escala del objeto.

### 2026-11-07 (7) — Componentes tapados: no se ven ni se eligen

**Añadido**
- En los modos Puntos/Aristas/Polígonos ya no se dibuja lo que queda detrás de la propia
  malla. Se calcula con un z-buffer en CPU a media resolución, que se guarda en caché
  mientras no cambien la malla, la cámara ni el visor.
- **Solo Seleccionar Visibles** en la rueda de opciones de Selección Rectangular/Lazo,
  activa por defecto como en C4D. Con ella, el clic y el marco ignoran lo tapado. Sin
  ella, el marco elige también a través de la malla.

**Arreglado**: en la primera tanda de capturas del bloque anterior, los clics reales del
usuario entraban en la ventana de prueba. El script de captura ahora espera a que no se
toque el PC y descarta la captura si hubo input.

**Probado**: compila sin avisos y pasan los tests. De frente, la cara de delante se ve
y la de detrás no; se ven 4 puntos.
Verificado por captura limpia, sin input del usuario:
- cubo editable en Polígonos: «2 de 6», sin diagonales ni aristas ocultas;
- seleccionar todo con Solo Visibles: 7 de 8 puntos (el de detrás no);
- sin la opción: 6 de 6 polígonos.

**Sin probar**: con el ratón de verdad y con OpenGL.

**Limitaciones conocidas**:
- Solo tapa la propia malla, no otros objetos.
- Los triángulos que cruzan el plano de la cámara no tapan.
- Una arista o un polígono cuenta como visible o tapado según su punto medio o su centro:
  uno tapado a medias puede quedar del lado equivocado.

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
