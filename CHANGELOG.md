# Cambios de Iron3D Studio

Todavía no hay versiones numeradas: Iron3D está en desarrollo temprano y nada de esto es
una versión terminada. Cada bloque lleva la fecha en que entró, qué se probó y qué no.
Las notas largas, en tono de blog, están en [`docs/devlog/`](docs/devlog/).

## Sin publicar

## Traducciones completas en los 6 idiomas restantes

- Polaco, checo, ruso, japonés, coreano y chino tenían ~890 textos sin traducir (menús, tooltips, mensajes, atributos, biblioteca, modelado, render); ahora los 11 idiomas cubren el 100 % de la interfaz.
- Cada traducción se validó contra el original: mismos marcadores `%s/%d/%lld`, mismos saltos `\n` y mismos sufijos `##id`.

## Escalar Proyecto y Guardar Proyecto con su Contenido

- **Escalar Proyecto…** (Ajustes del Proyecto) ya funciona: multiplica por el factor la posición y la escala de los objetos de primer nivel y las claves de posición y escala; se puede deshacer.
- **Guardar Proyecto con su Contenido…** (menú Archivo) ya funciona: guarda el `.iron` y copia las texturas usadas a una carpeta `tex` junto a él; los materiales quedan apuntando al nombre del archivo.

**Sin probar:** ambas solo se han compilado; no se han ejecutado en la interfaz. Los textos nuevos no están en los archivos de idioma (salen en español).
**Sigue pendiente:** Entorno, Primer Plano y Jiggle, Selección de Claves, Grabar Parámetro y Nivel de Puntos, Cambiar Vista (multi-vista), Selector Visual, Luz IES con `.ies`, Esculpir y el resto de la lista de límites.

## Objeto Entorno

- **Crear > Escena > Entorno** ya crea un objeto (antes la entrada no hacía nada). Tiene **Color Ambiente** y **Nivel**, y controla la luz ambiente del visor, en Vulkan y en OpenGL: un color más claro o un nivel más alto aclara las zonas en sombra. Con el valor por defecto (gris 0,25, nivel 1) el visor queda como antes.
- Se guarda y se carga en el `.iron` (campo `lv`), y no cuenta como Fondo o Cielo para el fondo del render.

**Sin probar:** compila y los tests pasan, pero no se ha mirado el efecto en el visor ni hay un test propio del Entorno. No afecta al render por CPU del Visor de Imágenes. Los textos nuevos salen en español en los demás idiomas.
**Sigue pendiente:** Primer Plano y Jiggle (sin función), y el resto de límites.

## Objeto Primer Plano

- **Crear > Escena > Primer Plano** ya crea un objeto (antes la entrada no hacía nada). Mezcla un degradado (color superior e inferior, opacidad ajustable) sobre el render del Visor de Imágenes. Por defecto es negro al 30 %, un viñeteado simple.
- Se guarda y se carga en el `.iron`.

**Sin probar:** compila, pero no se ha mirado el render con el objeto en la interfaz. No afecta al visor en tiempo real, solo al render del Visor de Imágenes. Los textos nuevos salen en español en los demás idiomas.
**Sigue pendiente:** Jiggle y el resto de límites.

## 27 deformadores y generadores nuevos

Las entradas de Deformadores, Modelado y MoGraph que no hacían nada ahora crean objetos que funcionan. Todos siguen el patrón de los deformadores existentes: actúan sobre el primer hijo con malla (o sobre todos los hijos, según el tipo), son no destructivos y se recalculan al cambiar el hijo o un parámetro.

- **Deformadores:** Jiggle (muelle sobre la posición animada del hijo), FX Explosión, Romper, Corrección, Cubrir, Superficie, Envoltura, Cámara (rejilla de 4 esquinas), Colisión, Desplazador (ruido 3D), Fórmula (con su propio intérprete: x y z u v t, sin cos tan sqrt abs exp log noise pi), Viento, Suavizado y Malla (jaula: el segundo hijo deforma al primero).
- **Modelado:** Matriz, Formación de Átomos, Booleano (unión, sustracción, intersección entre los dos primeros hijos), Conector (une y suelda las mallas hijas), LOD (por distancia a la cámara), Reducción de Polígonos y Metaball (tetraedros en marcha sobre los hijos).
- **MoGraph:** Matriz MoGraph, Fractura, Fractura Voronoi, MoInstance (estela de la posición animada), MoExtrude y PolyFX.
- Se guardan en el `.iron`: segundo parámetro, fórmula y jaula en reposo del deformador Malla.
- Test nuevo `deform_more_test`: comprueba que todos los tipos dan una malla válida y mide el efecto de 12 de ellos.

**Límites:**
- Fractura Voronoi no pone tapas en los cortes.
- Colisión y Cubrir comparan contra todos los triángulos del otro objeto; con mallas muy grandes no hacen nada.
- Metaball trata cada hijo como una esfera del tamaño de su caja.
- El deformador Spline y Rail/Envolver Spline siguen deshabilitados: necesitan splines.
- Nada de esto se ha mirado todavía en la interfaz; solo compila y pasa los tests.

### Splines y generadores de splines (2026-11-24)

**Añadido**
- Objeto Spline (`src/core/Spline.*`) con 16 tipos: libre (puntos, lineal o suave), Arco, Círculo, Hélice, n-Lados, Rectángulo (con redondeo), Estrella, Texto (fuente del sistema vía stb_truetype), 4-Lados, Cisoide, Rueda Dentada, Cicloide, Fórmula (X/Y/Z de t), Flor, Perfil (H/L/T/U/Z) y Vectorizador (contorno de una imagen). Plano XY/ZY/XZ, panel de atributos, edición de la lista de puntos y "Convertir en Spline Editable".
- Los splines se dibujan en el visor (seleccionado en amarillo, generados en azul, cruces en los puntos de control) y se seleccionan con un clic.
- Herramientas de visor Lápiz (clic añade punto), Dibujo (arrastrar) y Spline Arco (arco entre el último punto y el clic), sobre el suelo o el plano de la vista. Suavizado Spline relaja los puntos.
- Booleanas de splines (Unión, Sustraer, Y, O, Intersección) y Máscara de Spline: rasterizado + marching squares, resultado aproximado.
- Generadores: Extrusión (con tapas y agujeros), Torno, Forro, Recorrido (escala final y giro), Bezier (4x4 puntos de control editables), MoText, Trazador (trayectoria animada de los hijos), MoSpline (crecimiento), y los deformadores Spline, Rail de Splines y Envolver Spline.
- Hacer Editable convierte también splines paramétricos y generadores (se queda la malla o el spline que se ve).
- Guardado y carga de splines (`SPL`) y de los parámetros nuevos de los deformadores al duplicar.
- Test `spline_test` (áreas de tapas con agujero, extrusión, torno, forro, recorrido, booleanas, texto, generadores).

**Corregido**
- Duplicar un deformador perdía `param2`, la fórmula y la jaula en reposo.

**Límites conocidos**
- Las booleanas de splines son aproximadas (rejilla de 384 celdas) y no conservan las curvas originales.
- Rail de Splines usa un solo rail (el ancho), no dos.
- Los puntos de un spline se editan en la lista del panel o se añaden con Lápiz/Dibujo; aún no se arrastran en el visor.
- Sin tangentes Bezier por punto: la interpolación suave es Catmull-Rom.
- Nada de esto está comprobado a mano en la interfaz todavía; solo compilación, tests y arranque.

### Animación: parámetros, Nivel de Puntos y Hoja de Claves (2026-11-24)

**Añadido**
- Grabar Parámetro: pistas de Fuerza y Parámetro 2 de los deformadores y generadores, medidas de los splines (A, B, C, ángulos), intensidad, ángulo y color de las luces, distancia focal de la cámara y nivel del Entorno. Se interpolan como las demás pistas (Spline, Lineal, Escalón).
- Grabar Nivel de Puntos (PLA): guarda la forma de la malla en el fotograma; entre claves interpola cada punto en línea recta (o Escalón).
- Selección de Claves: con el botón activo, F9 y la clave automática solo graban en las pistas que el objeto ya tiene.
- Hoja de Claves en el panel alto de la Línea de Tiempo: una fila por pista (Posición, Rotación, Escala, cada parámetro, Nivel de Puntos), claves en rombo con color por interpolación, clic para seleccionar, arrastrar para mover, clic derecho para interpolación o eliminar, clic en vacío para cambiar el fotograma. Botón para abrir el Editor de Curvas.
- Mostrar Pistas / F-Curves / Movimientos abren el Editor de Curvas.
- Menú Malla > Spline: Interpolación Dura/Suave, Unir Segmento, Explotar Segmentos, Asignar Primer Punto, Invertir Secuencia, Mover Secuencia Arriba/Abajo, Suavizado Spline, Contornear (copia a 20 cm), Alinear, Redondear y las cinco booleanas.
- Centrar Eje a Objeto (mallas y splines editables; los hijos no se mueven).
- Guardado y carga de pistas de parámetros (`KEY ch=a:<id>`) y de claves de Nivel de Puntos (`PLA`).
- Test `anim_test` (evaluación, mover/interpolación de PLA, ida y vuelta por el guardado de parámetros, PLA y splines).

**Límites conocidos**
- El Editor de Curvas sigue mostrando solo Posición, Rotación y Escala; las pistas de parámetros y PLA están en la Hoja de Claves.
- Romper Segmento, Bisel, Sección Transversal, Proyectar, Igualar tangentes y Centrar Eje a Selección siguen desactivados: necesitan selección de puntos de spline o tangentes.
- Sin comprobar a mano en la interfaz; solo compilación, tests y arranque.

### Objetos que faltaban y Configuración del Proyecto (2026-11-24)

**Añadido**
- Cubo, Esfera y Plano con medidas editables: al cambiar Tamaño, Radio, Segmentos u Orientación pasan a primitiva paramétrica (Cubo con segmentos X/Y/Z, Esfera con segmentos, Plano con segmentos y orientación ±X/±Y/±Z).
- Objetos nuevos en la paleta: Tanque de Petróleo (radio, altura, altura de la tapa), Figura (maniquí de 8 cabezas), Terreno Fractal (ruido fractal con semilla y bordes a nivel), Relieve (alturas desde una imagen), Polígono (cuadrado o triángulo), Polígono Vacío y Guía (línea recta de 20 m).
- Configuración del Proyecto: Usar Animación, Usar Expresiones (IK y Protección), Usar Generadores y Usar Deformadores ya funcionan (apagados, generadores y deformadores enseñan su primer hijo tal cual). Nivel de Detalle escala la distancia de cambio de nivel de los objetos LOD.

**Límites conocidos**
- Los parámetros de las primitivas paramétricas no se guardan en el .iron (al cargar vuelven como malla), igual que las demás primitivas paramétricas.
- La Configuración del Proyecto no se guarda con la escena; Usar Sistema de Movimiento, color por defecto, recorte de vista y flujo lineal siguen desactivados.
- La Figura es un maniquí de cajas, sin articulaciones.
- Sin comprobar a mano en la interfaz; solo compilación, tests y arranque.

### Visor, escena y cámaras (2026-11-24)

**Añadido**
- Mostrar: Sombreado Gouraud (Líneas), Líneas, Alambre y Caja (las líneas son las aristas de polígono, sin las diagonales de la triangulación; en Gouraud (Líneas) solo las caras que miran a la cámara).
- Paneles: 2 Vistas Apiladas y 2 Vistas Lado a Lado, además de Vista Simple y 4 Vistas. El icono Cambiar Vista de la esquina alterna entre vista simple y multivista.
- Cielo Físico: cielo con un Sol (Luz infinita hija) cuyos colores, dirección e intensidad siguen la Hora (0–24 h) del panel.
- Nube, Herramienta de Nubes, Grupo de Nubes y Unir Nubes: nubes hechas con un generador Metaball y esferas ocultas al azar; Unir Nubes junta las esferas de las nubes seleccionadas en la primera.
- Césped: briznas al azar (anchura, profundidad, altura, número de briznas y semilla).
- Grúa: base, Brazo y Cabeza encadenados con una cámara al final.
- Cámara de Movimiento: cámara con temblor de mano grabado como claves en el rango de previo.
- Morph de Cámara: con dos o más cámaras seleccionadas, crea una cámara que pasa de una a otra a lo largo del rango de previo.
- Herramienta de Iluminación: pone un foco en el punto de vista actual apuntando al objeto seleccionado.

**Límites conocidos**
- Siguen desactivados: Sombreado Rápido/Constante, Líneas Ocultas, Isoparms, Esqueleto, Axonométrica, Estereoscópico y Cámara Estéreo.
- Las nubes son metaballs opacos, no volumétricos. El Cielo Físico no tiene atmósfera ni nubes propias, y la hora no se puede animar.
- La Grúa no tiene parámetros propios: se maneja rotando sus nulos.
- El Morph de Cámara copia las posiciones y rotaciones como claves; si cambias las cámaras de origen después, no se actualiza.
- Sin comprobar a mano en la interfaz; solo compilación, tests y arranque.

### Panel de atributos: campos que eran de solo lectura (2026-11-24)

**Añadido**
- Básico: Usar Color (Desactivado/Automático/Activado) con Color de Visualización, que el visor usa en los objetos sin material, y Rayos X, que dibuja el objeto semitransparente. Se guardan en el .iron.
- Nulo: Mostrar (Punto, Círculo, Rectángulo, Diamante, Triángulo, polígonos, Estrella, Eje, Cubo, Pirámide, Esfera o Ninguno), Radio, Relación de Aspecto y Orientación son editables.
- Etiqueta Phong: Limitar Ángulo y Ángulo de Phong recalculan las normales de los objetos poligonales. Cada esquina pesa por su ángulo, así que un cuadrado partido en dos triángulos se suaviza igual que uno entero.
- Etiqueta de Textura: Proyección, Lado, Repetición, Sin Costuras, Desfase U/V, Longitud U/V y Repeticiones U/V son editables.

**Límites conocidos**
- Phong no parte vértices: una arista dura solo aparece donde la malla ya tiene vértices separados. No se aplica a primitivas paramétricas ni a mallas generadas.
- El visor solo pinta la proyección UVW; con las demás proyecciones la textura sale con color plano. Siguen desactivados Usar Bordes, Mezclar Texturas, Usar UVW para Relieve y el Orden de rotación.
- Sin comprobar a mano en la interfaz; solo compilación, tests y arranque.

### Pinceles de Esculpir (2026-11-24)

**Añadido**
- Esculpir > Pinceles: Estirar, Agarrar, Suavizar, Encerar, Cortar, Pellizcar, Achatar, Inflar, Amplificar, Rellenar, Raspar y Borrar, sobre objetos editables. Se arrastra en el visor.
- Ventana del pincel: Tamaño en píxeles (con el círculo junto al ratón), Presión y Simetría X. Ctrl invierte el pincel.
- Esculpir > Simetría Esculpir activa y desactiva la simetría en X.
- Borrar devuelve la zona a como estaba la malla al empezar a esculpir ese objeto.
- Cada trazo se deshace de una vez con Ctrl+Z.

**Límites conocidos**
- Siguen desactivados el pincel Repetir, Seleccionar, Máscara, Reducir/Incrementar los niveles de subdivisión, Bakear, Proyectar Malla, Des-Subdividir y Esculpir a Pose Morph. No hay niveles de escultura: se esculpe directamente sobre la malla.
- El pincel busca vértices dentro del radio sin mirar si están conectados con la zona tocada. Un trozo cercano de otra parte de la malla también se mueve.
- En mallas muy densas cada paso recalcula todas las normales, así que puede ir lento.
- Sin comprobar a mano en la interfaz; solo compilación, tests (spline_test cubre Estirar, invertir, Achatar, Suavizar, Borrar y Agarrar) y arranque.

## Gestor de objetos, materiales y visor (2026-11-24)

**Gestor de objetos:** Modo Lista y Modo Árbol, Mostrar Etiquetas, Establecer como Raíz (con barra "< Escena" y Mostrar Todo), Fusionar Objetos desde Archivo, etiquetas Fong, Protección, Mostrar y Objetivo (el Objetivo crea un Nulo y lo asigna a la cámara o luz).

**Materiales:** Nuevo Material PBR, Cargar/Guardar Materiales (.imat), Guardar Todos, Cargar/Guardar Material Preestablecido, Deshacer, Cargar y Editar Textura.

**Visor:** Paralela orbitable y vistas axonométricas (Isométrica, Dimétrica, Militar, Caballera, Pájaro, Rana); modos Rápido, Constante, Líneas Ocultas y Esqueleto; Opciones (OpenGL Mejorado, Sombras, Transparencia, SSAO, Normales de polígonos y vértices, Texturas, Rayos X); Filtrar por tipo de objeto; Redibujar.

**Arreglos:** la línea DISP (Usar Color, Rayos X, Phong) se guardaba pero no se podía volver a cargar; Rayos X no hacía transparente el objeto.

**Límites conocidos:** etiquetas Compositing y Restricción, Reflexiones, Teselación, Profundidad de Campo, Ocultar Caras Posteriores, Isoparms, Estereoscópico y Configurar siguen desactivados; el Modo Lista no pliega; las vistas oblicuas no tienen rejilla corregida.

## Render por CPU: luces de la escena, texturas horneadas y partes con material (2026-11-25)

Reto de Freddy: se renderizó Freddy_Fazbear [VF_2.0] de `fnaf 1 tcg final update.lib4d` con las luces "Freddy Thank you lights" del pack de FazbearRenders.

**Cambios en el render (Mayús+R y `--ui-cmd=render-save=`):**
- Usa las luces de la escena (difusa y especular, caída de C4D, focos) en vez de dos luces fijas de cámara.
- El color de los materiales con árbol de shaders (Filtro, Capa, Colorizador, Degradado) sale horneado, igual que en el visor. Antes los trajes salían blancos.
- Respeta las partes con otro material (Selecciones de Polígonos de C4D); antes se ignoraban.
- Proyecciones de textura Esférica, Cilíndrica, Plana, Cúbica y Frontal, ajustadas a la caja del objeto (aproximación: la etiqueta no trae su propia matriz).
- Variable `IRON3D_DBG_TEX=1` para ver qué texturas hornea.

**Resultado honesto:** el render sale lejos de la referencia. Freddy queda en T-pose (sin posar), la cabeza sale negra (la máscara `freddymaskara dos.png` de Suit_1_Head, el pelaje de la cabeza no se resuelve) y no hay desenfoque, fondo ni acabado de Cinema 4D.

**Límites conocidos:** el visor sigue dibujando las proyecciones que no son UVW en color plano; sin posado desde la línea de comandos; luz Ambiente sin modelar.

## FBX y Alembic: importar y exportar (2026-11-25)

Con permiso de Rooty se añaden dos librerías, vendorizadas en `third_party/`:
- **ufbx** (MIT/dominio público, un solo `.c`) para leer FBX.
- **Alembic 1.8.9** (BSD-3, solo el núcleo Ogawa, sin HDF5 ni Boost) con **Imath 3.1.9** (se descarga con CMake).

**FBX (Archivo > Abrir/Fusionar/Exportar):**
- Importa FBX binario y ASCII: jerarquía, mallas triangulizadas, normales, UV, materiales (color base, metal, rugosidad, emisión) y textura de color por ruta.
- Exporta FBX ASCII 7.4 con jerarquía, mallas, normales, UV, materiales por polígono y transformaciones locales.
- Probado en `fbx_test`: ida y vuelta con posición, rotación, color de material y número de triángulos.
- Sin probar con FBX de otros programas (no hay ninguno en el disco). Sin animación ni esqueleto: se avisa en el informe.

**Alembic (Archivo > Abrir/Fusionar/Exportar):**
- Importa `.abc` Ogawa: Xform, PolyMesh y SubD (primer fotograma, con normales y UV si las trae).
- Exporta la escena como Xform + PolyMesh estáticos.
- Probado en `abc_test`: ida y vuelta de jerarquía, matriz, triángulos y UV.
- Alembic guarda las caras en sentido horario: se invierte al leer y al escribir. Esa convención sale de la documentación, no se ha contrastado con un `.abc` de Maya o Houdini.
- Sin HDF5 (los `.abc` antiguos no abren), sin animación de vértices, cámaras, curvas ni puntos.

**Los tests que faltaban:** `iron3d_tests`, `ik_test` y `obj_test` se ejecutaron y terminan con código 0.

## Splines y Gestor de Atributos: Romper Segmento, Bisel, Copiar/Pegar (2026-11-25)

- **Malla > Spline > Romper Segmento:** como todavía no se seleccionan puntos del spline, rompe por la mitad: un spline cerrado se abre ahí, uno abierto se parte en dos segmentos.
- **Malla > Spline > Bisel:** corta cada esquina (todas si es cerrado, las interiores si es abierto) a 10 unidades como mucho o al 40 % del lado, y pasa a interpolación dura. Sin parámetros ajustables todavía.
- **Gestor de Atributos > Editar > Copiar / Pegar:** copia los atributos del objeto activo y los pega en otro del mismo tipo (posición, visibilidad, luz, cámara, spline, clonador, deformador, entorno...). No copia nombre, jerarquía ni animación. Pegar entre tipos distintos queda desactivado.
- Los textos nuevos todavía no están traducidos a los 11 idiomas: salen en español.

## Datos de Usuario, Tomas, cámara estéreo, fondo desenfocado, pose y cabeza de Freddy (2026-11-25)

- **Datos de Usuario** (Gestor de Atributos > Datos de Usuario): Añadir (nombre y tipo Real, Entero, Booleano o Texto), editar el valor en la página Básico del objeto, y Gestionar (renombrar, cambiar tipo, borrar). Se guardan con la escena, se copian al duplicar y con Pegar. Probado en `anim_test` (ida y vuelta).
- **Tomas** (Etiquetas > Gestor de Tomas, Añadir a la Toma Activa, Eliminar de Todas las Tomas): la toma Principal es la escena base; cada toma extra guarda anulaciones de visibilidad en editor y render, posición, rotación y escala de los objetos que le añades. Al cambiar de toma, la escena vuelve a la base y se aplican las de la elegida; lo que editas con una toma activa se guarda en ella. Se guardan con la escena. Probado en `anim_test`. No anulan parámetros distintos de esos cinco (materiales, luces, deformadores) ni tienen render por lotes.
- **Cámara estéreo:** en Configuración de Render, Estéreo = anaglifo rojo-cian o lado a lado, con separación de ojos en cm (6,5 por defecto). Menú del visor > Opciones > Estereoscópico lo activa. Solo afecta al render; el visor sigue en mono. Verificado por captura con Freddy (anaglifo). Comando: `--ui-cmd=stereo=<0|1|2>,<cm>`.
- **Fondo con imagen y desenfoque:** Configuración de Render > Imagen de fondo y Desenfoque (px). El fondo se coloca detrás del modelo por máscara de color clave y se desenfoca con tres pasadas de caja. Los bordes del modelo pueden mostrar un halo oscuro fino. Comando: `--ui-cmd=bg-image=<ruta>|<px>`.
- **Pose por línea de comandos:** `--ui-cmd=pose=<objeto o ruta>:H,P,B` gira ese objeto antes de `render-save`.
- **Cabeza de Freddy:** salía negra porque el Filtro con Luminosidad −0,67 oscurecía ×0,33. Contrastado con un render real de R19 (la cabeza sale marrón, factor ≈ 0,665), la Luminosidad negativa del Filtro pasa a actuar a la mitad (×(1 + L/2)). Es un único dato medido: la rama positiva no está contrastada y otras librerías con Filtro de Luminosidad negativa cambiarán de aspecto.
- Los textos nuevos no están traducidos a los 11 idiomas: salen en español.

### 2026-11-24 (49): Atajos de Vista y Fijación al girar/escalar

**Añadido**
- **Ctrl+Mayús+Z / Ctrl+Mayús+Y** deshacen y rehacen la vista (como en Cinema 4D), tal como anunciaba el menú Vista. Antes Ctrl+Mayús+Z rehacía la escena; ahora el rehacer de la escena es **Ctrl+Y**.
- **Fijación** al girar (pasos de 5°) y al escalar (pasos de 0,1), además de al mover; respeta los ejes bloqueados y solo cuantiza los ejes que se tocan. Se aplica antes de la etiqueta Protección, así que un límite nunca se sobrepasa por redondeo.

**Corregido (nota)**
- El límite «Vista Solo no oculta los iconos de cámara ni de luz» estaba desactualizado: los iconos y la selección con el ratón ya respetan Vista Solo. Lo que sigue igual es que las luces ocultas por Vista Solo **siguen iluminando** la escena.

**Verificado:** compila y pasan las pruebas; los atajos y la Fijación no se han probado a mano con el ratón en esta sesión.

**Límites:** la Fijación al girar y escalar usa pasos fijos (5° y 0,1), sin ajustes propios todavía.

### 2026-11-24 (48): Etiqueta Límite / Protección de C4D

**Añadido**
- Iron3D lee la etiqueta **Protección** (la «Límite» de los controladores de los rigs): por cada eje de posición, escala y rotación guarda el modo (Libre, Bloquear o Límite) y el rango mínimo/máximo. Se importa desde .c4d, se guarda en .iron (línea `PROT`) y se copia al duplicar.
- **Bloquear** impide mover ese eje con el gizmo y con los campos de Coordenadas; **Límite** recorta el valor al rango. Sin «Permitir expresiones», el límite también se aplica a la IK y a lo que mueva la escena en cada fotograma (los canales con animación se respetan).
- Pestaña de Atributos: sección **Protección** (solo si el objeto la tiene) para cambiar modo y rango por eje.

**Verificado:** el modelo real con 71 etiquetas Protección las importa con sus modos (p. ej. dedos con rotación bloqueada en un eje); pruebas nuevas en `ik_test`; todas las pruebas pasan.

**Límites:** los ids de la etiqueta se han tomado de `tprotection.h`; los modos se ven correctos en el modelo real, pero los rangos Límite no se han contrastado contra R19 (las 71 etiquetas del modelo solo usan Bloquear/Libre). No se puede añadir la etiqueta desde cero en la interfaz (solo editar la importada), y no se exporta de vuelta a .c4d.

### 2026-11-24 (47): Rutas de objetos con nombres repetidos (IK, Piel, objetivos)

**Corregido**
- Las rutas por nombre que usan las etiquetas IK (cadena, objetivo, polo), la Piel (articulaciones), los objetivos de cámara y luz y las instancias apuntaban siempre al **primer** hermano con ese nombre. En rigs de C4D es habitual que varios hermanos se llamen igual, y entonces la cadena IK o la articulación de la Piel quedaban atadas al objeto equivocado y no hacían nada. Ahora el 2.º, 3.º… hermano homónimo se escribe `Nombre~#2`, `Nombre~#3`, y todos los puntos que resuelven rutas (IK, Piel, Deform, exportar a .c4d) usan la misma regla. Los `.iron` antiguos siguen abriéndose (sin sufijo = el primero).
- El aviso al importar «Iron3D todavía no resuelve la IK» ya no sale: la IK se resuelve desde 2026-11-19.

**Herramientas**
- `c4d_probe` con `IRON3D_IK_TEST=1` resuelve las etiquetas IK de un .c4d, mueve los controladores y mide si la punta los sigue; `IRON3D_DUMP_TAGS=1` lista tip/objetivo/polo de cada IK.

**Verificado:** con un .c4d real (dos cadenas IK de ojos) la punta sigue al controlador movido (error 0,0005 sobre un hueso de 0,36); pruebas nuevas de nombres repetidos en `ik_test`; todas las pruebas pasan.

**Límites:** no he podido reproducir el fallo exacto del modelo que importó Rooty (no sé cuál era); el arreglo cubre la causa que he encontrado (nombres repetidos). Siguen sin hacerse: IK 2D/3D distinguidas por solver, etiqueta Límite de C4D, Selector Visual y alternar IK/FK.

### 2026-11-24 (46): Información de versión en el exe y en el instalador

**Añadido**
- `Iron3DStudio.exe` lleva ahora, en Propiedades → Detalles: descripción, versión del archivo y del producto, nombre del producto, empresa, copyright e idioma (español e inglés). La versión sale de un único sitio, `IRON3D_VERSION_*` en `CMakeLists.txt`, y se genera `iron3d.rc` desde `resources/iron3d.rc.in`.
- El instalador `Iron3DStudio-Setup-<versión>.exe` también lleva descripción, versión, empresa y copyright.

**Límites:** al subir de versión hay que igualar a mano `IRON3D_VERSION_*` (CMake) y `MyAppVersion` (`installer/iron3d_setup.iss`). La 0.3.0-alpha ya publicada no lleva estos datos; saldrán en la próxima release.

**Verificado:** leyendo `VersionInfo` del exe y del instalador compilados.

### 2026-11-24 (45): Vista Solo, Modo Ajuste, Plano de Trabajo y Modo Textura

**Añadido**
- **Vista Solo**: el visor dibuja solo el objeto seleccionado y sus descendientes (también para sombras y para elegir con el ratón). Sin selección se ve todo.
- **Modo Ajuste**: oculta el gizmo; el clic agarra el objeto (o los componentes) y lo arrastra directamente, con la herramienta activa (Mover, Rotar o Escalar).
- **Plano de Trabajo**: la cuadrícula de la vista en perspectiva toma la posición y la orientación del objeto seleccionado. **Bloquear Plano de Trabajo** la deja donde está.
- **Modo Textura**: panel flotante con la proyección, el desplazamiento U/V, la longitud U/V y el mosaico de la etiqueta de Textura del objeto seleccionado, con deshacer.
- Traducciones de las cadenas nuevas al inglés, alemán, francés e italiano.

**Límites:** Vista Solo no oculta las luces ni los iconos de cámara. El Modo Ajuste no agarra ejes ni planos concretos; usa el arrastre libre. El Plano de Trabajo solo mueve la cuadrícula: crear, mover o fijar objetos no se apoya en él, y las vistas ortogonales mantienen su plano. El Modo Textura no tiene manipulador en el visor; solo campos numéricos, y el visor y el render solo dibujan texturas con proyección UVW.

**Verificado con tests:** compila y los tests existentes pasan. **Sin ver en pantalla ni probar con ratón:** los cuatro botones.

### 2026-11-24 (44): Buscar y Filtrar del Gestor de Objetos, y efectos de estudio en el render

**Añadido**
- **Buscar** (lupa del Gestor de Objetos): filtra la lista por nombre, sin distinguir mayúsculas. Se muestran también los padres de lo que coincide.
- **Filtrar** (embudo): muestra u oculta Geometría, Luces, Cámaras y Otros.
- **Efectos del render por CPU** (Configuración de Render → Efectos): reflejos de estudio con Fresnel, Transparencia del material (los triángulos transparentes se ordenan y se mezclan) y sombra suave en el suelo.
- `--ui-cmd=render-fx` activa los efectos y `--ui-cmd=render-save=...` guarda el render en PNG sin interfaz (para verificar).
- Traducciones de las cadenas nuevas al inglés, alemán, francés e italiano.

**Límites:** el filtro no oculta nada en el visor 3D, solo en la lista. Los efectos son una aproximación: sin refracción, sin reflejos del propio objeto y sin sombras de objetos entre sí; el cristal oscuro sale lavado. Solo afectan al render del Visor de Imágenes, no al visor interactivo.

**Verificado con tests:** compila y los tests existentes pasan. **Verificado por captura:** los renders de logos de cristal. **Sin ver en pantalla:** Buscar y Filtrar.

### 2026-11-24 (43): Bloqueo de ejes y Fijación

**Añadido**
- **Bloqueo de ejes** (botones X, Y y Z de la barra): un eje bloqueado no cambia de posición, rotación (X = H, Y = P, Z = B) ni escala al arrastrar el gizmo. Con varios objetos seleccionados el bloqueo también llega a los demás.
- **Fijación** (botón de la barra o Mayús+S): al mover con el gizmo la posición salta de 100 cm en 100 cm, el espaciado de la cuadrícula.
- `--ui-cmd=render` renderiza la vista activa sin teclado (para verificar).
- Traducciones de las cadenas nuevas al inglés, alemán, francés e italiano.

**Límites:** la Fijación solo actúa al mover (no al rotar ni escalar), solo con la cuadrícula de 100 cm y sobre la posición local del objeto; no encaja con puntos ni aristas. Siguen sin implementar Modo Ajuste, Vista Solo, Plano de Trabajo, Modo Textura, Buscar y Filtrar del Gestor de Objetos.

**Verificado con tests:** el bloqueo (arrastre libre con X bloqueado no mueve X y sí Y). **Sin ver en pantalla ni probar con ratón:** la Fijación y los botones. **Verificado por captura** (sobre la 0.3.0-alpha ya publicada): Explosión, Cámara Objetivo, Luz IES, Luz PBR y el render con Cielo (degradado azul de fondo). FFD con los valores por defecto no deforma nada, como se espera.

### 2026-11-24 (42): Bisel, Explosión, FFD, Morph, objetivos de Cámara y Luz, Luz IES/PBR, Fondo y Cielo

**Añadido**
- **Deformadores nuevos**: **Bisel** (achaflana aristas y vértices; un cubo pasa a 26 polígonos), **Explosión** (separa los triángulos a lo largo de su normal), **Morph** (mezcla el primer hijo con el segundo si tienen los mismos puntos) y **FFD** (rejilla 2x2x2 con las ocho esquinas editables). Se guardan en el `.iron`.
- **Cámara Objetivo** y **Luz Objetivo**: crean el objeto y un Nulo «Objetivo»; el objeto se orienta solo hacia él. La ruta del objetivo se guarda y se edita en el Gestor de Atributos.
- **Luz IES** (foco con decaimiento cuadrático) y **Luz PBR** (área con decaimiento físico y sombra).
- **Fondo** y **Cielo**: objetos de escena que ponen el degradado de fondo del render en el Visor de Imágenes (color superior e inferior editables).
- Panel de atributos de la **Cámara**: proyección, distancia focal, tamaño del sensor, campo de visión y objetivo.
- Traducciones de las cadenas nuevas al inglés, alemán, francés e italiano.

**Corregido**
- El objetivo de cámaras y luces miraba al lado contrario (el motor mira por -Z).

**Límites:** Luz IES no carga perfiles `.ies`; Luz Solar no es un sol físico; Fondo y Cielo solo afectan al render del Visor de Imágenes, no al visor interactivo; **siguen sin hacer nada**: Entorno, Primer Plano y Jiggle. Polaco, checo, ruso, japonés, coreano y chino tienen traducidas solo unas 480 cadenas; el resto sale en inglés. Esculpir y las herramientas interactivas de Malla siguen sin estar, y varios botones de la barra siguen marcados «todavía no implementado».

**Verificado por captura:** Bisel y Luz Objetivo (el foco ilumina la cima del cubo). **Verificado con tests:** Explosión, FFD, Morph, Bisel, orientación de la cámara hacia su objetivo y guardado y lectura de Fondo/Cielo, FFD y objetivo de luz. **Sin ver en pantalla:** Explosión, FFD, Morph, Cámara Objetivo, Luz IES, Luz PBR y el render con Fondo/Cielo.

### 2026-11-20 (41): Subdivisión Superficie, Instancia, deformadores nuevos, Cámara, Luz Solar y Suelo

**Añadido**
- **Subdivisión Superficie** (Catmull-Clark en vivo): el objeto con malla se pone como hijo y el generador muestra la superficie suavizada; niveles 1 a 4, normales suaves, el hijo hace de jaula y se sigue editando aparte. Un cubo pasa a 24 cuadriláteros en el nivel 1 y a 96 en el 2.
- **Instancia**: comparte la malla de otro objeto y la sigue en vivo cuando este cambia. Se enlaza por la ruta de nombres y se guarda en el `.iron` (línea `INST`).
- **Deformadores nuevos**: Aplastar y Estirar, Esferizar y Derretir (se suman a Doblar, Torsión, Sesgar, Ahusar y Abultar).
- **Cámara**, **Luz Solar** y **Suelo** ya crean objetos desde el menú Crear. El sol orienta la luz hacia abajo.
- Los argumentos de la línea de comandos (`--ui-cmd`, rutas) se leen ahora en UTF-8, así que los nombres con tilde funcionan.
- Traducciones de las cadenas nuevas al inglés, alemán, francés e italiano.

**Límites:** Luz Solar es una luz infinita con color cálido y sombras suaves, no un sol físico. Siguen sin hacer nada: Cámara Objetivo, Luz Objetivo, Luz IES, Luz PBR, Cielo, Fondo, Entorno, Primer Plano, Bisel, FFD, Explosión, Jiggle y Morph.

**Verificado por captura:** Subdivisión Superficie, Doblar, Derretir, Instancia, Luz Solar (cima y caras iluminadas), Cámara y Suelo en el Gestor de Objetos. **Verificado con tests:** Aplastar y Estirar, Esferizar, Derretir, Catmull-Clark (recuentos y normales unitarias) e Instancia (malla compartida, seguimiento, guardado y lectura).

### 2026-11-20 (40): Traducciones al alemán, francés e italiano

**Añadido**
- Alemán, francés e italiano tienen ahora traducidas todas las cadenas nuevas de las últimas sesiones: menús de Malla, mensajes de las herramientas, Información del Objeto, atajos de teclado (Strg/Umschalt, Maj, Maiusc), Preferencias, tooltips y exportación SMD.

**Límites:** polaco, checo, ruso, japonés, coreano y chino tienen solo las traducciones oficiales de los nombres de Cinema 4D (unas 480 cadenas); el resto de la interfaz sale en inglés en esos idiomas.

**Verificado por captura:** menú Malla > Comandos en alemán (`--lang=de`).

### 2026-11-20 (39): Malla: Mover/Escalar/Rotar Normal, Matriz y Configuración Modelado

**Añadido**
- **Mover Normal**: cada punto seleccionado (o los de las aristas/polígonos seleccionados) se desplaza 10 cm por su normal. **Escalar Normal**: escala la selección ×1,25 a lo largo de su normal media respecto a su centro. **Rotar Normal**: la gira 15° alrededor de su normal media por su centro. Se pueden repetir y deshacer.
- **Matriz**: 3 copias de los polígonos seleccionados (o de toda la malla) desplazadas en X el ancho de la selección más un 10 %; las copias quedan seleccionadas.
- **Configuración Modelado** abre Preferencias.

**Límites:** los valores son fijos (sin diálogo de parámetros ni arrastre interactivo como en Cinema 4D). Siguen en gris: Crear Punto, Pincel, Lápiz Polígonos, Magnetismo, Puntada y Cosido, Extrusión Matriz, Suavizar Desplazamiento, Desconectar, Asignar Valor al Punto, Aristas a Spline y Cambiar Orden de Puntos.

**Verificado:** compilación y tests de las funciones en el cubo. **Sin probar:** en pantalla.

### 2026-11-20 (38): Opciones de verificación para abrir menús y diálogos sin clics

**Añadido** (solo para probar la interfaz, no cambian nada al usarla)
- `--ui-cmd=menu=Malla,Comandos`: abre esos menús y submenús tal como se ven en pantalla (uno de nivel superior a la vez).
- `--ui-cmd=obj-info`, `--ui-cmd=about` y `--ui-cmd=prefs-page=N`: abren Información del Objeto, Acerca de y una página de Preferencias.

**Verificado por captura de pantalla (2026-11-20, PC en reposo):** menú Malla > Comandos (Fundir, Disolver, Clonar, Conectar, Voltear Arista y Restaurar Escala activos), Esculpir > Pinceles (todo en gris: el escultor no está hecho), Ayuda, Información del Objeto, página Navegación de Preferencias, Archivo (Cerrar y Guardar Todo activos; Volver al guardado y Guardar incrementalmente en gris mientras no haya archivo) y Vista (Deshacer/Rehacer Vista en gris sin historial).

### 2026-11-20 (37): Menú Archivo: Volver al guardado, Cerrar, Guardar incrementalmente y Guardar Todo

**Añadido**
- **Volver a Archivo Guardado**: recarga el archivo actual descartando los cambios (pregunta antes si los hay).
- **Cerrar** y **Cerrar Todos**: dejan un proyecto nuevo (Iron3D trabaja con un solo proyecto a la vez); preguntan si hay cambios sin guardar.
- **Guardar incrementalmente**: guarda junto al archivo actual como nombre_001.iron, nombre_002.iron... (siguiente número libre) y el proyecto pasa a ser ese. Necesita que el proyecto ya tenga archivo.
- **Guardar Todo**: igual que Guardar (solo hay un proyecto abierto).

**Límites:** Guardar Proyecto con su Contenido sigue en gris.

**Verificado:** compilación. **Sin probar:** en pantalla ni con archivos reales.

### 2026-11-20 (36): Deshacer Vista y Rehacer Vista

**Añadido**
- **Vista > Deshacer Vista / Rehacer Vista** (antes en gris): cada visor guarda hasta 50 puntos de vista (tipo de vista, objetivo, distancia y ángulos). Un movimiento de cámara cuenta como uno cuando la cámara se queda quieta unos instantes.

**Límites:** los atajos Ctrl+Mayús+Z/Y de la etiqueta del menú aún no están enlazados al teclado; solo funciona desde el menú.

**Verificado:** compilación y tests. **Sin probar:** en pantalla.

### 2026-11-20 (35): Menú Ayuda activo e Información del Objeto

**Añadido**
- **Ayuda**: Mostrar Ayuda, Mostrar Tutoriales y Soporte de Static Forge abren staticforge.me en el navegador; Comprobar Actualizaciones abre la página de versiones del repositorio.
- **Información del Objeto** (clic derecho en el Gestor de Objetos): nombre, tipo, posición, rotación, escala, hijos y, si es una malla, puntos, aristas, polígonos (triángulos, cuadriláteros, N-gons) y tamaño en cm.

**Límites**
- Aprendizaje Online, Instalación Manual, Registro Online y Personalizar siguen en gris.

**Verificado:** compilación y tests. **Sin probar:** los dos en pantalla.

### 2026-11-20 (34): Fundir, Reflejar, Restaurar Escala y cortes Plano/Cíclico en el menú

**Añadido**
- **Fundir** (puntos, aristas o polígonos): como Disolver; con puntos une todo lo que rodea al punto.
- **Reflejar**: copia reflejada de los polígonos seleccionados (o de toda la malla) respecto al plano X, Y o Z del objeto (el que esté activo en Simetría de edición; X por defecto). La copia queda seleccionada.
- **Restaurar Escala**: la escala del objeto pasa a los puntos y el objeto queda a escala 1, con la misma forma en pantalla. No se aplica si el objeto tiene hijos.
- **Cortar Plano** y **Cortar Cíclico/Trayectoria** del menú Malla activan el Cuchillo en su modo Plano y Bucle (antes salían en gris).

**Verificado:** compilación y tests (Reflejar sobre el cubo). **Sin probar:** los comandos en pantalla.

### 2026-11-20 (33): Planchar, Voltear Arista, Clonar y Conectar

**Añadido**
- **Planchar** (puntos seleccionados, o toda la malla si no hay): cada punto se acerca a la media de sus vecinos, 3 pasadas al 50 %; los puntos de un borde abierto no se mueven y las normales se recalculan.
- **Voltear Arista** (modo Aristas): gira la diagonal que comparten dos triángulos, salvo que el cuadrilátero resultante sea cóncavo.
- **Clonar** (modo Polígonos): copia los polígonos seleccionados encima de los originales y deja la copia seleccionada.
- **Conectar** (puntos): parte cada polígono que tenga dos puntos seleccionados no vecinos con una arista entre ellos.

**Límites**
- Conectar con aristas parte por los extremos de las aristas, no por sus puntos medios. Planchar no tiene aún ajustes de intensidad.

**Verificado:** compilación y tests sobre el cubo. **Sin probar:** los comandos en pantalla.

### 2026-11-20 (32): Disolver y polos de la esfera

**Añadido**
- **Disolver** (menú Malla y clic derecho, modo Aristas o Polígonos): une en un solo polígono los que comparten las aristas seleccionadas (en modo Polígonos, las aristas que comparten los seleccionados). Deja la malla como estaba y avisa si el resultado no sería un polígono plano, convexo y de un solo contorno.

**Corregido**
- La esfera de Iron3D ya no usa cuadriláteros degenerados en los polos: ahí las caras son triángulos.

**Límites**
- Disolver con puntos seleccionados sigue sin existir.

**Verificado:** compilación y tests (cubo: la arista entre caras no coplanares se deja; en la tapa subdividida dos cuadriláteros pasan a uno). **Sin probar:** el comando en pantalla.

### 2026-11-20 (31): Los modelos .c4d importan con sus polígonos reales (cuadriláteros y N-gons)

**Corregido**
- Al importar un `.c4d`, cada cuadrilátero se guardaba como dos triángulos y el editor tenía que adivinar cuáles iban juntos; un cubo podía salir como 12 triángulos sueltos y dos triángulos consecutivos casi coplanares se pegaban por error. Ahora el importador marca exactamente los polígonos que trae C4D: un cubo importado son **6 polígonos y 8 puntos**, como en Cinema 4D.
- **N-gons:** se leen del `NgonBase` del objeto (cada N-gon lista los polígonos que lo forman) y se juntan en un solo polígono de Iron3D. Probado con archivos reales: `reifenmantel` de `cs_buildon.c4d` sale con 146 N-gons, el modelo de `MODELOS fox.c4d` con los suyos, y `stairs.c4d`/`knot.c4d` con todos sus cuadriláteros (594 y 11 520) sin triángulos sueltos.
- Los polígonos de un mismo N-gon se mantienen seguidos al reordenar por parte de material.
- La herramienta `c4d_probe` muestra ahora puntos, polígonos y cuántos son triángulos, cuadriláteros y N-gons de cada malla.

**Límites**
- Un N-gon con agujero (varios segmentos) o cuyos polígonos queden separados por un material distinto se divide en polígonos sueltos en vez de perderse.
- Las escenas `.iron` guardadas antes de este cambio con cubos de triángulos sueltos no se migran: unir triángulos coplanares podría estropear mallas importadas legítimas.
- La exportación a `.c4d` sigue escribiendo triángulos y cuadriláteros, sin N-gons.

**Verificado:** compilación, tests (incluye cubo = 6 polígonos y un N-gon de 6 lados) e importación de archivos `.c4d` reales con el probe. **Sin probar:** el aspecto en el editor de un modelo con N-gons.

### 2026-11-20 (30): Menú contextual de modelado como el de C4D, menú Malla completo y Preferencias con árbol

**Añadido**
- **Clic derecho en el visor con un objeto editable en modo Puntos/Aristas/Polígonos**: ahora sale el menú de modelado de Cinema 4D (Deshacer Acción, Encuadrar Elementos, Crear Punto, Puente, Cerrar Agujero, Cortar Línea, Soldar, Bisel, Extruir, Extrusión Interior, Normales, Colapso, Optimizar, Separar, Subdividir, N-gons…). En modo Objeto sigue el menú de siempre.
- **Menú Malla** reconstruido con sus submenús (Conversión, Comandos, Herramientas Crear, Herramientas Transformar, Spline, N-Gons, Normales, Centrar Ejes, Simetría de edición), **Esculpir** (con Pinceles) y **Ayuda**.
- Operaciones nuevas que funcionan: **Optimizar** (une puntos a menos de una tolerancia en cm, quita puntos sueltos y polígonos sin área; rueda dentada con la tolerancia), **Colapso** (cada grupo de polígonos seleccionados pasa a un punto; con puntos, los suelda), **Separar** (los polígonos seleccionados pasan a un objeto nuevo), **Eliminar N-gons** y **Retriangular N-gons**.
- **Preferencias** con el árbol de páginas de C4D (Interfaz, Navegación, OpenGL, Renderizador…); solo están activas las páginas que cambian algo. Nueva página **Navegación**: velocidad de Pan, Zoom y Órbita (10–500 %) y Órbita invertida, aplicadas al momento y guardadas. Botón para abrir la carpeta de preferencias.

**Límites**
- Las entradas que aún no existen (Crear Punto, Magnetismo, Reflejar, Conectar, Disolver, Fundir, Desconectar, herramientas de Spline, Pinceles de Esculpir, Ayuda…) salen en gris, como en C4D sin función. «Desconectar» no se puede representar porque los puntos se unifican por posición.
- Las demás páginas de Preferencias están desactivadas hasta que tengan ajustes reales.
- Idiomas ru/pl/cs/zh/ja/ko: lo nuevo sale en inglés.

**Verificado:** compilación, 9 suites de tests (incluye Optimizar y Colapso sobre el cubo). **Sin probar:** los menús y las Preferencias en pantalla (Iron3D seguía abierto y no se ha relanzado el exe nuevo).

### 2026-11-20 (29): El idioma se cambia al momento, sin reiniciar

**Corregido**
- Al elegir un idioma en Preferencias > Interfaz solo se guardaba para el siguiente arranque y la interfaz seguía igual, así que parecía que no funcionaba. Ahora el cambio se aplica **al instante** (menús, paneles y atributos) y además se guarda para la próxima vez.
- El idioma que se pasa con `--lang=<código>` en la línea de comandos manda sobre el de Preferencias en esa sesión; por eso, si se abría Iron3D con ese parámetro, elegir otro idioma al reiniciar no tenía efecto. Elegirlo en Preferencias lo cambia igualmente en la sesión.

**Límites**
- Los textos que algún panel ya guardó al abrirse pueden quedarse un momento en el idioma anterior hasta que se vuelvan a abrir.
- Ruso, polaco, checo, chino, japonés y coreano siguen **sin traducción completa**: lo que falta sale en inglés.
- Los diccionarios de los idiomas que se dejan atrás se conservan en memoria hasta cerrar el programa (unos cientos de KB cada uno).

**Verificado:** compilación. **Sin probar:** el cambio en pantalla.

### 2026-11-20 (28): Aviso de guardar al cerrar, Guardar con el diálogo del sistema y caras seleccionadas sin diagonal

**Añadido**
- **Aviso al cerrar con cambios sin guardar**, como en Cinema 4D: «¿Quiere guardar los cambios en el proyecto "Sin Nombre 1" antes de salir?» con **Sí / No / Cancelar**. Si no se ha tocado nada, se cierra sin preguntar. **Sí** abre el diálogo de guardar del sistema (si el proyecto aún no tiene nombre) y cierra al terminar; si en ese diálogo se cancela, el programa sigue abierto como estaba. **No** cierra sin guardar. **Cancelar** no hace nada. Vale para el botón de cerrar la ventana, Alt+F4 y Archivo > Salir.
- El mismo aviso al **Nuevo** y al **Abrir** (menú, atajos Ctrl+N / Ctrl+O y el Comandante) cuando hay cambios.
- **Guardar (Ctrl+S)** y **Guardar como (Ctrl+Mayús+S)** usan el diálogo de guardar del sistema (`.iron`, nombre propuesto «Sin Nombre 1.iron»). Guardar vuelve a escribir sobre el archivo que ya tiene nombre sin preguntar. Antes abrían un cuadro propio con una carpeta fija.
- Test: la instantánea de la escena (lo que decide si hay cambios) es estable si no se toca nada, cambia al mover un objeto y vuelve a coincidir al deshacer el movimiento.

**Corregido**
- Al seleccionar un **polígono** se veía una **diagonal** que lo partía en dos triángulos: cada triángulo se rellenaba por separado y el suavizado dejaba una costura. Ahora un polígono convexo se rellena de una vez; los cóncavos siguen rellenándose por triángulos.

**Límites**
- «Hay cambios» se decide comparando la escena entera con la guardada, no con un contador de ediciones: cambiar solo la cámara, la selección o la línea de tiempo no cuenta.
- Abrir un `.c4d`, `.obj` o `.glb` deja el proyecto «sin nombre» (hay que guardar como `.iron`).
- El aviso y el diálogo de guardar solo existen en Windows; en Linux se mantiene el cuadro propio de la carpeta `scenes/` y el cierre sin aviso.
- Los textos nuevos del aviso aún no están traducidos a los demás idiomas (salen en español).
- No se guarda aún la lista de archivos recientes ni el título de la ventana con el asterisco de cambios.

**Verificado:** tests y compilación. **Sin probar:** el aviso real al cerrar (cuadro de Windows), el guardado con el diálogo y el relleno del polígono en el visor.

### 2026-11-20 (27): Cubo, Esfera y Plano sin caras partidas; contorno de selección que rodea la figura

**Corregido**
- Al **Hacer Editable** un Cubo, cada cara se veía partida en dos triángulos (con la diagonal marcada y la cara seleccionándose a mitades). Las primitivas Cubo, Esfera y Plano no declaraban sus cuadriláteros (`faceTris` vacío), así que el modelado los trataba como triángulos sueltos. Ahora nacen con sus polígonos: el Cubo tiene 6 caras, el Plano 1 y la Esfera un cuadrilátero por celda. Vale también para los hijos de Simetría/Clonador/Deformadores, que usan las mismas mallas.
- El **contorno naranja** del objeto seleccionado ya no es la caja envolvente (se quedaba como un cubo que no seguía la figura al mover puntos o caras): ahora es la silueta de los vértices reales. Con mallas de más de 60 000 vértices se mantiene la caja por rendimiento.
- En los modos **Puntos, Aristas y Polígonos** ya no se dibuja ese contorno naranja, como en C4D, donde solo se ven las aristas del objeto.
- Tests: las tres primitivas cubren todos sus triángulos con `faceTris` y el Cubo/Plano tienen 6/1 polígonos.

**Límites**
- Escenas guardadas antes de este cambio con un Cubo ya editable mantienen su malla con triángulos sueltos; hay que rehacer el objeto.
- Los polos de la Esfera siguen teniendo cuadriláteros degenerados (dos puntos coincidentes), como en la malla anterior.
- El contorno usa la envolvente convexa: una figura con huecos o cóncava queda rodeada por fuera, no pegada a su silueta real.

**Verificado:** tests y compilación. **Sin probar:** el aspecto en el visor.

### 2026-11-20 (26): Simetría, Clonador y Deformadores con Cubo, Esfera y Plano

**Corregido**
- La **Simetría no hacía nada con un Cubo, una Esfera o un Plano**: los generadores solo veían hijos con malla poligonal, y esos tres objetos son paramétricos sin malla propia. Seleccionar un Cubo y pulsar Simetría creaba un Nulo vacío, y arrastrar el Cubo dentro tampoco funcionaba. Lo mismo pasaba con el **Clonador** y los **Deformadores**.
- Ahora, al evaluar un generador, sus hijos Cubo, Esfera o Plano se convierten en objetos poligonales con la misma malla (como Hacer Editable) y funciona tanto al crearlo con el objeto seleccionado como al arrastrar el objeto dentro de un generador ya creado. Si el generador creado con el menú tiene un paramétrico seleccionado, lo envuelve como ya hacía con los poligonales.
- Tests: Simetría con un Cubo hijo (se convierte, sale el reflejo con el mismo número de vértices y queda en x negativa), Clonador con una Esfera hija y Deformador con un Plano hijo.

**Límites**
- El hijo **deja de ser paramétrico** al convertirse (no hay forma de volver; sí Deshacer). Para Cubo, Esfera y Plano no cambia nada visible: su tamaño sale de la escala del nodo.
- Las primitivas nuevas (Cilindro, Cono, Toroide…) ya eran objetos con malla y no se veían afectadas.
- Sigue sin haber **Simetría de Spline** ni «Soldar puntos» como en C4D.

**Verificado:** tests y compilación. **Sin probar:** en el visor ni con clics reales.

### 2026-11-20 (25): Gestor de Objetos: arrastrar para hacer hijos y combinaciones de modificadores al crear

**Añadido**
- **Arrastrar y soltar objetos en el Gestor de Objetos** (antes no existía): se agarra una fila y se suelta sobre otra. El tercio de arriba de la fila lo pone **encima como hermano** (línea ámbar arriba), el centro lo mete **dentro como hijo** (recuadro ámbar) y el tercio de abajo **debajo como hermano**. Soltar en la zona vacía del panel lo lleva al final de la raíz. Si hay varios objetos seleccionados y agarras uno de ellos, se mueven todos juntos (solo los de más arriba; sus hijos van con ellos) manteniendo su orden. Los objetos **no se mueven en el mundo** al cambiar de padre. Un objeto no se puede soltar dentro de sí mismo ni de uno de sus descendientes (no sale la marca y no pasa nada). Un solo paso de deshacer.
- **Combinaciones de teclas al crear un objeto** con uno seleccionado, tomadas de la referencia de C4D (`c4d_ui_reference_visual.md` §18.6), en menús y paletas: **Alt** = hijo del seleccionado, **Ctrl** = hermano debajo, **Ctrl+Mayús** = hermano arriba, **Alt+Ctrl** = padre de toda la selección (la selección pasa a ser hija del objeto nuevo), **Alt+Mayús** = un objeto nuevo como hijo de cada seleccionado.
- El menú **Objetos > Añadir** del Gestor de Objetos antes creaba siempre en la raíz y ignoraba el Alt, y solo tenía Cubo, Esfera, Plano y Luz. Ahora ofrece las mismas primitivas y luces que las paletas y respeta Alt/Ctrl/Mayús. «Añadir Hijo» del menú contextual también usa esa lista y siempre crea dentro del objeto elegido.
- Tests de `SceneNode::reparentNodes` / `canReparentNodes`: no meterse en sí mismo ni en un descendiente, la raíz no se mueve, conserva la posición en el mundo, orden dentro del mismo padre (al principio y al final) y varios nodos a la vez.

**Límites**
- **El arrastre y las combinaciones de teclas no se han probado con ratón y teclado reales** (solo la lógica de mover nodos tiene tests). Falta comprobar sobre todo que el arrastre no choca con el clic de selección y que Alt+clic en menús llega bien.
- Alt+Mayús con varios seleccionados deja un paso de deshacer por objeto creado.
- Las zonas hijo/hermano son fijas (25 % / 50 % / 25 % de la altura de la fila); no hay autoexpandir al mantener el cursor sobre una fila plegada ni autodesplazamiento al arrastrar cerca del borde.
- Los objetos con efecto sobre su hijo (Simetría, Clonador, deformadores) se pueden arrastrar como los demás, pero no se avisa si queda un hijo que ya no cumple su papel.

**Verificado:** tests y compilación. **Sin probar:** arrastrar con el ratón y los atajos con teclado real.

### 2026-11-20 (24): Luces como en C4D: estrella y gizmos por tipo

**Cambiado**
- La luz ya no se dibuja como esfera negra: ahora es una **estrellita blanca de líneas** en su posición (14 rayos, tamaño fijo en escena, sin escala del nodo), amarilla si está seleccionada.
- Según el tipo se añade el gizmo de C4D: **Foco** y **Foco cuadrado** (cono hasta un círculo/cuadrado, con eje central; el radio sale del ángulo exterior), **Paralela con foco** (cilindro/prisma), **Infinita/Paralela** (flecha hacia +Z), **Área** (rectángulo o disco del tamaño de la luz, con una marca hacia donde ilumina) y **Tubo** (una línea). Omni y Fotométrica: solo la estrella.
- El gizmo gira con la luz (apunta hacia +Z local, como en C4D).
- Tests: 14 rayos dentro del radio, Omni sin líneas extra, radio del cono = tan(ángulo/2) × largo, flecha de la Infinita, rectángulo del Área con la mitad del tamaño a cada lado, la estrella centrada en la luz y luz oculta sin gizmo.

**Límites**
- La longitud del cono y de la flecha es fija (250 cm), no sigue el radio de decaimiento real.
- **Sin tiradores** (los puntos naranjas de C4D) para cambiar ángulo o tamaño arrastrando: se edita en el panel de Atributos.
- Para elegir una luz con el ratón se sigue usando una esfera invisible del tamaño de la estrella, no las líneas del gizmo.
- Tubo y Fotométrica son aproximaciones; la forma real del área distinta de rectángulo/disco no está.

**Verificado:** tests y compilación. **Sin probar:** el aspecto en el visor (Vulkan y OpenGL) ni la selección con clic reales.

### 2026-11-20 (23): Seleccionar Patrón

**Añadido**
- Menú Selección, en modo Puntos, Aristas o Polígonos: **Patrón: 1 de cada 2**, **1 de cada 2 (desfasado)** y **1 de cada 3**. Parten de la selección actual y se quedan con una de cada N (los dos desfases de «cada 2» juntos reconstruyen la selección original). También con `--ui-cmd=meshop=pattern2|pattern2b|pattern3`.
- Tests con selección conocida: 1 de cada 2, desfasado, 1 de cada 3, paso 1 / vacío sin cambios y reconstrucción con los dos desfases.

**Límites**
- El orden es el de los **índices de la topología**, no el recorrido geométrico de un bucle: en mallas generadas por Iron3D suele coincidir con el orden de creación, pero en una malla editada a mano puede dar saltos.
- Solo N = 2 y 3 desde el menú (la función admite cualquier paso y desfase, falta un diálogo).
- Sobre la petición de ocultar puntos y aristas: **no se ha tocado**. Ocultar ya funciona en modo Puntos/Aristas/Polígonos, pero oculta *polígonos* (los que tienen todos sus puntos elegidos), no puntos o aristas sueltos. Ocultarlos de verdad exige cambios en el dibujado del visor y queda pendiente.

**Verificado:** tests y compilación. **Sin probar:** en el visor ni con clics reales.

### 2026-11-20 (22): Clonador: modo Objeto

**Añadido**
- Cuarto modo del Clonador, **Objeto**: clona el primer hijo con malla en cada punto del **segundo** hijo con malla (los vértices repetidos, como los 24 del Cubo, cuentan como un solo punto; máximo 1000). Si mueves o editas el segundo hijo, las copias se recolocan en vivo. Con un solo hijo no genera nada.
- Se guarda en el `.iron` (modo 3 del bloque `CLN`). Escala por copia, giro por copia y efector Aleatorio siguen valiendo en este modo.
- Tests: 8 puntos únicos en un cubo, una copia por punto, copias en las esquinas, sin puntos no hay copias, un solo hijo no genera, dos hijos sí, no recalcula sin cambios y recoloca al mover el destino.

**Límites**
- Las copias **no se orientan** según la normal del punto ni se reparten por superficie/volumen: solo vértices.
- El segundo hijo no se oculta solo: hay que apagar su visibilidad en el editor si no se quiere ver.
- Sin selección de puntos ni peso por vértice como en C4D.

**Verificado:** tests y compilación. **Sin probar:** en el visor ni con clics reales.

### 2026-11-20 (21): Clonador: efector Aleatorio

**Añadido**
- Sección **Efector Aleatorio** en el Clonador: posición aleatoria máxima por eje (cm), giro aleatorio (°), escala aleatoria (%) y **semilla**. Cada copia recibe su propio valor; con la misma semilla la disposición es idéntica siempre, y cambiar la semilla da otra.
- Se guarda en el `.iron` (claves `rpx/rpy/rpz/rrot/rscale/seed` del bloque `CLN`; los archivos anteriores siguen abriendo) y se copia con el nodo.
- Tests: reproducibilidad, respeto del máximo, ejes a 0 sin moverse, cambio con la semilla y escala dentro del rango.

**Límites**
- Es un efector incrustado en el Clonador, no un objeto Efector independiente: no se puede compartir entre varios Clonadores ni acotar con falloff/campos.
- Distribución uniforme (no hay Gauss, ruido ni Aleatorio de Color/Peso).
- Las traducciones de las etiquetas nuevas (de/fr/it) no las ha revisado un hablante nativo.

**Verificado:** tests y compilación. **Sin probar:** aspecto en el visor y uso con clics reales.

### 2026-11-20 (20): Clonador (Lineal, Cuadrícula, Radial)

**Añadido**
- **Clonador** funcional en la paleta MoGraph (antes decorativo). Con una malla seleccionada la envuelve; sin selección crea un nodo vacío al que se le arrastra un objeto como hijo. Genera copias del primer hijo con malla y lo oculta (el hijo se sigue editando aparte; borrar el Clonador deja el hijo intacto). No destructivo y en vivo, como los Deformadores y la Simetría.
- Modos: **Lineal** (paso X/Y/Z en cm por copia), **Cuadrícula** (cantidad X/Y/Z y separación, centrada en el nodo) y **Radial** (radio y eje; las copias miran hacia fuera). Extras: **Escala por copia** (%) y **Giro por copia** (°). Máximo 1000 copias y 2 millones de vértices.
- Guardado en `.iron` (bloque `CLN`), copia/pegado y deshacer; `--ui-cmd=create=Cubo+Clonador`.
- Tests: conteos (lineal 4, cuadrícula 3×2×2 = 12, radial 6), límites geométricos (lineal hasta x=3.5, cuadrícula centrada, radial plano en el eje), escala por copia, tope de 1000, el nodo genera/no recalcula sin cambios/recalcula al cambiar la cantidad y viaje de ida y vuelta por el `.iron`.

**Límites**
- Las copias son **una sola malla fusionada**, no instancias: con clones muy pesados no ahorra memoria.
- Todavía **no hay** efectores (Aleatorio, Plano, Paso), Modo Objeto (clonar sobre los puntos de otro objeto), Modo Rejilla de tipo Hex ni color/peso por copia.
- Cada copia hereda el material del hijo; no se puede dar un material distinto por copia.
- Traducciones de las etiquetas nuevas a de/fr/it sin revisar por un hablante nativo.

**Verificado:** tests y compilación completa. **Sin probar:** el aspecto en el visor y el uso con clics reales (el PC estaba en uso y las capturas se descartan).

### 2026-11-20 (19): Booleanos (Unión, Sustracción, Intersección)

**Añadido**
- Tres comandos en el menú de objetos, junto a Conectar Objetos: **Booleano: Unión (A + B)**, **Sustracción (A - B)** e **Intersección (A y B)**. Con dos objetos seleccionados (Ctrl+clic en el Gestor de Objetos; A = el primero de la lista, B = el segundo) crean un Objeto poligonal nuevo con el resultado y dejan A y B ocultos (se pueden volver a mostrar). Funcionan con primitivas, objetos poligonales, paramétricos y deformados; si A o B están movidos/escalados/rotados se respeta su posición en el mundo.
- Algoritmo: CSG por árboles BSP (`src/core/Boolean.cpp`). Los triángulos que atraviesa el otro objeto se parten; el resultado son polígonos convexos (con `faceTris`) y los vértices coincidentes se sueldan.
- Órdenes `--ui-cmd=bool-union`, `bool-sub` y `bool-int` (sobre la selección actual).
- Test de volumen: dos cubos solapados dan unión 1.5, A − B 0.5 e intersección 0.5; con cubo y esfera se cumplen V(A∪B)+V(A∩B)=V(A)+V(B) y V(A−B)=V(A)−V(A∩B). Un solape vacío o una malla vacía devuelven error.

**Límites**
- **No es paramétrico**: genera una malla fija (no hay objeto Boole con A/B como hijos que se actualice al mover). Si mueves A o B después, hay que rehacer la operación.
- Cada malla se parte por el plano de cada uno de sus triángulos: con mallas de miles de polígonos el resultado puede tener muchos polígonos y tardar; se rechaza con más de 6000 triángulos entre las dos.
- Hace falta que A y B sean **cerrados**. Con mallas abiertas el resultado no es fiable; de momento no se avisa.
- No hay «Crear Cantos Vivos», selección de intersección ni mantener UV/material de B.
- Los mensajes de error que vienen del núcleo no están traducidos (solo el prefijo).

**Hallazgo:** el sentido de giro de los triángulos del Cubo base es mixto (solo +Z mira hacia fuera con la regla de la mano derecha; el visor no descarta caras traseras). El Booleano orienta cada triángulo por la normal de sus vértices; otras herramientas que dependan del sentido de giro deberían hacer lo mismo.

**Verificado:** tests de volumen y casos límite; compila y se enlaza en la aplicación. **Sin probar:** el flujo con clics reales y el aspecto en el visor (el PC estaba en uso y no se lanzó la captura).

### 2026-11-20 (18): Deformadores Doblar, Enroscar, Afilar, Hinchar y Sesgar

**Añadido**
- Cinco deformadores no destructivos en Crear > Deformador y en la paleta de Deformadores (antes todos deshabilitados): **Doblar, Enroscar, Afilar, Hinchar y Sesgar**. Funcionan como el objeto Simetría: con un objeto con malla seleccionado se mete dentro de un nodo deformador; el nodo dibuja la copia deformada y el original queda oculto en el Gestor de Objetos (se sigue editando ahí y el resultado se actualiza al instante). Borrar el nodo y mostrar el hijo devuelve el objeto intacto.
- Atributos > Objeto del deformador: **Tipo** (cambiable después), **Eje** (X/Y/Z, a lo largo del cual actúa) y **Ángulo** (Doblar, Enroscar, en grados) o **Fuerza** (Afilar, Hinchar, Sesgar, en %).
- Se guardan y se leen en `.iron` (bloque `DEF`) y se conservan al copiar/duplicar. Textos traducidos a inglés, alemán, francés e italiano (la traducción italiana de los nombres de los deformadores no está contrastada con C4D).
- Orden `--ui-cmd=create=Cápsula+Doblar` (varias seguidas con `+`, también primitivas y deformadores).
- Tests: Afilar al 100 % lleva la cara de arriba a un punto; Enroscar conserva el radio (cara de arriba a 45°); Doblar cumple la fórmula del arco; Hinchar ensancha el medio sin mover los extremos; Sesgar; deformador sin hijo / con hijo / sin cambios / con parámetro nuevo; guardado y lectura.

**Límites**
- El deformador actúa sobre el **primer hijo con malla** (no sobre varios, ni sobre hijos de hijos, ni sobre jerarquías); no es todavía «hijo del objeto» como en C4D.
- No hay objeto Límite/Caja de deformación (el efecto cubre todo el objeto), ni Falloff, ni «Mantener longitud» en Doblar.
- Las normales se corrigen aproximadamente (Afilar y Hinchar las dejan sin tocar), así que el sombreado puede no ser exacto con deformaciones fuertes.
- El hijo queda con visibilidad del editor en «Apagado» y no se puede pinchar en el visor, solo en el Gestor de Objetos.

**Verificado:** tests de geometría y guardado. **Sin probar:** aspecto en el visor (la captura se pospuso porque había actividad en el PC), mallas con pocos polígonos (un cubo sin subdividir no se curva visiblemente con Doblar: hace falta geometría).

### 2026-11-20 (17): Primitivas poligonales con parámetros editables

**Añadido**
- Las ocho primitivas nuevas (Cilindro, Cono, Disco, Toroide, Cápsula, Tubo, Pirámide, Platónico) ya son **paramétricas**: en Atributos > Objeto se editan radio/altura/anchura (en cm), segmentos de rotación, del anillo, del tubo y de la tapa, y el Platónico tiene selector Tetraedro / Octaedro / Icosaedro / Dodecaedro. La malla se regenera al arrastrar.
- Se queda como Objeto poligonal normal: en cuanto se edita la malla (puntos, aristas, extrusión...), la primitiva pierde los parámetros y vuelve a ser un objeto editable corriente (y se ve la ficha de siempre).
- Clonar/copiar conserva los parámetros. Parámetros traducidos a inglés, alemán, francés e italiano.
- Test: cada tipo genera una malla válida (cerrada, salvo el Disco) y cambiar los parámetros cambia la malla.

**Límites**
- Los parámetros **no se guardan en la escena**: al reabrir un `.iron` la malla vuelve como Objeto poligonal sin parámetros.
- Cápsula: la altura mínima es 2 × el radio. Disco y Tubo: el radio interior se recorta justo por debajo del exterior.
- Sin Phong/redondeo ni orientación del eje (siempre Y).

**Verificado:** tests; captura del panel del Toroide en alemán (80/20 cm, 32/12 segmentos). **Sin probar:** arrastrar los campos con ratón real y deshacer/rehacer tras editarlos.

### 2026-11-20 (16): Primitivas poligonales: Cilindro, Cono, Disco, Toroide, Cápsula, Tubo, Pirámide y Platónico

**Añadido**
- Ocho primitivas nuevas en Crear > Objeto y en la paleta de Objetos (antes estaban deshabilitadas): **Cilindro, Cono, Disco, Toroide, Cápsula, Tubo, Pirámide y Platónico**. Se crean ya como Objeto poligonal editable, con cuadriláteros en los lados y n-gonos en las tapas (`src/core/PrimitiveShapes.cpp`), listas para Extruir, Bisel, Cuchillo, Bucle, etc. Medida base 1 unidad = 200 cm, como el Cubo.
- Orden `--ui-cmd=create=<Nombre>` para crear una primitiva sin input (verificación).
- Test en `c4d_geometry_test`: todas las primitivas cerradas pasan «Comprobar malla» sin bordes abiertos y con las normales de los triángulos coherentes con las de los vértices.

**Límites**
- Todavía **no son paramétricas** (no hay radio/segmentos editables en el Gestor de Atributos tras crearlas): salen con valores fijos. El Platónico sale como icosaedro; tetraedro, octaedro y dodecaedro existen en el código pero no hay selector en la UI.
- Disco y Tubo no tienen las opciones de C4D (radio interior, redondeo).

**Verificado:** tests de geometría (topología cerrada y orientación); captura del Toroide (429 vértices, 768 triángulos, 200×40×200 cm). **Sin probar:** el resto de primitivas a la vista, ni creación con clic real desde el menú.

### 2026-11-20 (15): Cuchillo en modo Plano y arreglo del rayo del Cuchillo en Línea

**Añadido**
- **Cuchillo (K) en modo Plano:** un clic en la malla fija un punto (el primer triángulo que toca el rayo) y corta con un plano perpendicular al eje local elegido (X, Y o Z) que pasa por ese punto. Con polígonos seleccionados, solo corta esos. El selector del panel ahora es Línea / Plano / Bucle. Orden `--ui-cmd=plane=x,y,eje` para verificar sin input.
- Cadenas nuevas traducidas a inglés, alemán, francés e italiano.

**Arreglado**
- **El Cuchillo en modo Línea trazaba el plano reflejado en vertical.** `screenRay` espera la proyección con la Y invertida (la que usa `main.cpp` para picar), pero el Cuchillo le pasaba la normal; solo coincidía con líneas simétricas respecto al centro del visor. Ahora usa la proyección correcta para el rayo y la normal para proyectar a pantalla. Comprobado con captura: una línea horizontal por el cubo corta las dos caras visibles (28 vértices, 16 triángulos).

**Límites:** el plano es siempre paralelo a un eje local (no se puede orientar libremente); no hay vista previa antes de hacer clic.

**No probado:** clic real con ratón (solo `--ui-cmd`); el resto de funciones que llaman a `screenRay` con la proyección sin invertir (queda pendiente revisarlas).

### 2026-11-20 (14): Cuchillo en modo Bucle

**Añadido**
- **Cuchillo (K) tiene ahora modo Bucle** además de Línea (selector en el panel de la herramienta). Un clic en una arista (a menos de 14 px) corta todo su anillo —las aristas opuestas de cuadrilátero en cuadrilátero— por el punto donde está el cursor a lo largo de la arista; cada cuadrilátero del anillo pasa a dos. «Solo Visibles» también vale aquí.
- `loopCut` en `MeshEdit`: solo parte cuadriláteros cortados por dos aristas opuestas; el resto de la malla queda intacta y los vértices nuevos comparten posición entre caras vecinas (la malla sigue cerrada). Interpola color, UV y normal.
- Cadenas nuevas traducidas a inglés, alemán, francés e italiano. Orden de línea `--ui-cmd=loop=x,y` para verificar sin input.
- Pruebas: en el cubo, un anillo parte 4 caras (10 caras, 12 puntos, 20 aristas, Euler 2, sin bordes abiertos); arista inválida no hace nada; la esfera corta sus cuadriláteros.

**Límites:** un solo corte por clic (sin varios cortes a la vez ni deslizar el punto antes de confirmar); no atraviesa triángulos ni n-gonos (el anillo se detiene ahí); la posición se mide desde el extremo de menor índice de cada arista, así que en anillos con aristas en sentidos opuestos el corte no sigue una proporción constante visual (con 0,5 no se nota).

**No probado:** el clic real con ratón (solo `--ui-cmd`); un anillo cerrado en una malla compleja (solo cubo y esfera en el test).

### 2026-11-20 (13): Valve SMD con esqueleto y pesos

**Añadido**
- **Archivo › Exportar › Valve SMD (Source)** ahora escribe el esqueleto: cada articulación (Ojoint) pasa a un hueso con su padre y su pose actual (posición y ángulos Euler de `studiomdl` respecto al padre); el hueso 0 es `root`. Los ejes y la escala son los de la malla.
- Las mallas con **Piel** exportan los pesos de cada vértice (hasta 3 huesos por vértice, el máximo de `studiomdl`; si hay más se conservan los 3 de mayor peso y se avisa). Lo que no suma 1 va a la articulación que contiene la malla o a `root`. Las mallas sin Piel van rígidas al hueso de la articulación que las contiene (o a `root`).
- El informe dice cuántos huesos y mallas con pesos salieron.
- `smd_test` comprueba nombres y padres de huesos, la posición (Y arriba a Z arriba) y el giro de la articulación en Z, y los enlaces de peso de tres vértices (uno a un hueso, uno repartido entre dos, uno sin pesos).

**Límites:** una sola pose (la actual, que se toma como pose de referencia), sin animación; no escribe el `.qc` ni los `.vmt`.

**No probado:** compilar el SMD con `studiomdl` ni verlo en Source. El convenio de ángulos (`R = Rz·Ry·Rx` en radianes) y el de pesos salen de la documentación general del formato, sin comprobar contra el motor. Tampoco he probado el menú con una escena rigada real (solo el test unitario).

### 2026-11-20 (12): Alemán, francés e italiano: el resto de la interfaz

**Añadido**
- Unas 340 cadenas más en alemán, francés e italiano (ayudas de las herramientas, Preferencias, Acerca de, importación y exportación, Navegador de Contenido, selección, Cuchillo, Puente, Cámaras y Vistas, avisos). Las tres lenguas quedan con la interfaz completa salvo los atajos de teclado, que no se traducen.
- El contador de selección de la vista ("Polígonos: 2 de 6 seleccionados") pasa por el traductor; antes salía con "de ... seleccionados" en español incluso en inglés.

**Probado:** captura en francés con un Cubo editable y dos polígonos seleccionados (menús, Atributos, contador y barra de estado en francés); el aplicador comprueba que cada traducción conserva los `%s/%d/%zu` y los saltos de línea de la cadena original. **No probado:** revisión por hablantes nativos (son traducciones propias, pueden tener giros mejorables); alemán e italiano por captura con todos los menús abiertos; ruso, polaco, checo, chino, japonés y coreano siguen con las cadenas oficiales de Cinema 4D más inglés como respaldo.

### 2026-11-20 (11): Exportar a Source (Valve SMD)

**Añadido**
- **Archivo › Exportar › Valve SMD (Source)**: escribe toda la escena como malla de referencia estática (un hueso `root`, una pose, triángulos con posición, normal y UV) lista para `studiomdl`. Ejes girados de Y arriba a Z arriba (`(x,y,z) → (x,-z,y)`), 1 unidad de Iron3D = 200 cm escalada a unidades de Source (1 unidad ≈ 1,905 cm), caras en sentido horario. El material es el nombre del archivo de la textura de Color (o el del material) en minúsculas: ese es el `.vmt` que hay que crear.
- Nuevo test `smd_test`.

**Límites:** sin esqueleto, pesos ni animación (la Piel de Iron3D no se convierte todavía) y no escribe el `.qc` ni los `.vmt`.

**Probado:** `smd_test` (nombres de material, 12 triángulos y 36 vértices de un Cubo, eje Z arriba y escala, escena vacía). **No probado:** compilar el SMD con `studiomdl` ni verlo en Source; las caras en sentido horario y la escala son lo que dice la documentación general del formato, sin comprobar contra el motor.

### 2026-11-20 (10): Alemán, francés e italiano: unas 260 cadenas propias más

**Añadido**
- Traducidas a de, fr e it las cadenas propias de Iron3D que antes caían al inglés: menús (Malla, Selección, Animación, Render), modos de componentes, luces, materiales, etiquetas, proyecto, línea de tiempo, Gestor de Materiales y las ayudas principales de las herramientas. Quedan unas 400 (sobre todo ayudas largas y mensajes de estado), que siguen saliendo en inglés. pl/cs/ru/ja/ko/zh siguen con las cadenas oficiales de C4D y el resto en inglés.

**Probado:** captura 1280x720 con `--lang=de` (menús, Gestor de Materiales, Coordenadas, cabeceras y Línea de Tiempo en alemán; ayudas largas aún en inglés). **No probado:** revisión de las traducciones por un hablante nativo; fr e it por captura.

### 2026-11-20 (9): Cerrar Agujero Poligonal con agujeros cóncavos

**Arreglado**
- **Cerrar Agujero** triangulaba el contorno en abanico, que solo vale para agujeros convexos: uno en forma de L daba triángulos que salían del contorno. Ahora, si el contorno es cóncavo, se triangula por orejas (un polígono por triángulo, porque los polígonos de Iron3D son convexos); si es convexo, sigue siendo un solo polígono.

**Probado:** `iron3d_tests` (prisma en L sin tapas: las dos tapas cóncavas se cierran con 4 triángulos cada una, la malla queda cerrada y con todas las caras orientadas de forma coherente). **No probado:** con el menú Malla en la interfaz; agujeros con puntos repetidos o contornos no planos.

### 2026-11-20 (8): .iron guarda los polígonos ocultos; arreglos de carga y de compilación

**Nuevo**
- Los **polígonos ocultos** (Selección > Ocultar) se guardan y se leen en el `.iron` (bloque `HIDDEN`, formato 9).

**Arreglado**
- La **etiqueta de Simetría** se escribía en el `.iron` pero el lector no reconocía el bloque `SYM` (no estaba en la lista de bloques del nodo): una escena guardada con simetría podía fallar al abrirse con «token inesperado». Ahora se lee, con prueba de ida y vuelta. (Deducido leyendo el código; no reproduje el fallo antes de arreglarlo.)
- `c4d_probe` (herramienta de consola) no enlazaba por falta de `Skin.cpp` en su lista de fuentes.

**Probado:** los 8 ejecutables de prueba pasan (`iron3d_tests` con ida y vuelta de SYM y HIDDEN); compila todo en Release.

### 2026-11-20 (7): Edición > Cortar, y Copiar/Pegar con selección múltiple

**Nuevo**
- **Cortar** (`Ctrl+X` y menú Edición): guarda una copia de la selección y la borra. Antes estaba desactivado.
- **Copiar/Pegar** guardan ahora una copia propia, no una referencia al objeto: se puede copiar, borrar el original y pegar. Copian toda la selección (varios objetos), y Pegar los deja junto al objeto activo (mismo padre) o en la raíz y los deja seleccionados.
- El primer Pegar tras Cortar conserva los nombres; los siguientes (y los de Copiar) añaden « copia».
- `--ui-cmd=cut|copy|paste` para verificar sin input.

**Verificado por captura:** `--select=Cubo --ui-cmd=cut --ui-cmd=paste --ui-cmd=paste` deja «Cubo» y «Cubo copia». **No probado:** `Ctrl+X`/`Ctrl+V` con teclado, selección múltiple y objetos con hijos.

### 2026-11-20 (6): Selección > Ocultar Selección, Ocultar No Seleccionados y Mostrar Todo

**Nuevo**
- **Ocultar Selección / Ocultar No Seleccionados / Mostrar Todo** (menú Selección, modos de componentes). Los polígonos ocultos dejan de dibujarse en el visor (el render usa una malla filtrada) y de tapar a los componentes de detrás; sus puntos y aristas dejan de mostrarse y de poder elegirse si solo los usan polígonos ocultos. En modo Puntos/Aristas se ocultan los polígonos cuyos puntos están todos elegidos. No deja ocultar el objeto entero.

**Probado:** `iron3d_tests` (la cara oculta no se ve y deja de tapar a la de detrás); compila Debug/Release. **Verificado por captura (sin input, `--ui-cmd=meshop=hideinv`):** el visor solo dibuja el polígono elegido. **No probado:** el clic en los menús con la interfaz abierta.
**Límites:** el ocultado se pierde al editar la malla (cualquier edición crea otra malla) y no se guarda en el `.iron`. En C4D el ocultado es por componente (puntos y aristas sueltos); aquí es por polígono.

### 2026-11-20 (5): Selección > Contorno y Rellenar Selección

**Nuevo**
- **Selección Contorno** (`U~Q`, cualquier modo de componente): de lo seleccionado se queda solo con el borde, los elementos que tocan algo sin seleccionar.
- **Rellenar Selección** (`U~F`, modo Polígonos): selecciona los huecos de polígonos sin seleccionar rodeados por la selección. Cada grupo de polígonos sin seleccionar unidos forma un hueco; el mayor se toma como «el exterior» y no se rellena.

**Probado:** `iron3d_tests` (cubo con la tapa subdividida: rellena el hueco pequeño y no la tapa de 4; contorno deja fuera la base interior). **No probado:** el clic en los menús con la interfaz abierta.
**Ojo:** la semántica exacta de C4D para estas dos órdenes no está documentada en el material que tengo (solo los ids 1011179 y 1011178); esta es una interpretación por nombre y comportamiento conocido, sin comparar con R19.

### 2026-11-20 (4): Malla > Destriangular

**Nuevo**
- **Destriangular** (Malla): une pares de triángulos vecinos en cuadriláteros cuando sus normales forman menos de 5° y el resultado es convexo. Con selección de polígonos actúa sobre ellos; sin selección, sobre toda la malla. Los triángulos sueltos se quedan como están.

**Probado:** `iron3d_tests` (los 12 triángulos de un cubo vuelven a 6 cuadriláteros, 8 puntos, malla cerrada; sin triángulos no cambia). **No probado:** el clic en el menú con la interfaz abierta; el umbral de 5° es fijo (C4D lo deja elegir) y no genera n-gons.

### 2026-11-20 (3): Interfaz en inglés por defecto y multilenguaje

**Nuevo**
- Iron3D arranca **en inglés** (es contra lo que se compara con Cinema 4D) y es multilenguaje como C4D: English, Español, Deutsch, Français, Italiano, Polski, Čeština, Русский, 日本語, 한국어 y 中文. El idioma se elige en **Preferencias > Interfaz > Idioma** (se guarda y se aplica al reiniciar) y en el **instalador** (diálogo de idioma al arrancar; lo deja en `language.cfg`). Orden de resolución: preferencias → `language.cfg` → inglés. `--lang=xx` lo fuerza sin guardarlo.
- Los textos viven en `resources/lang/<código>.txt` (se copian junto al exe). Los nombres de comandos salen de los `strings_*` oficiales de Cinema 4D (532 textos, p. ej. «Reproducir Hacia Delante» = «Play Forwards»); el resto del inglés está escrito a mano. Texto sin traducción → inglés → español.
- CJK: la fuente de sistema (Yu Gothic/Malgun/Microsoft YaHei) se fusiona con la de la interfaz; cirílico con rangos propios.
- Instalador de 10 idiomas (sin chino: Inno Setup no trae su `.isl`; el chino se elige dentro de la app).

**Probado**
- Compila (Debug y Release) y los tests de `iron3d_tests` pasan.
- Capturas de la ventana (sin input) en inglés, alemán, español y japonés: menús y barras traducidos; el japonés se dibuja bien.
- El instalador compila con ISCC.

**Sin probar / límites**
- Ejecutar el instalador (el diálogo de idioma y que escriba `language.cfg`) y comprobar cada panel con cada idioma.
- Alemán, francés, italiano, polaco, checo, ruso, japonés, coreano y chino traducen solo lo que trae C4D oficial (~480 textos cada uno); lo demás cae al inglés. El español es la clave original.
- Fragmentos de frase que se concatenan (« objetos convertidos.») siguen el orden del español/inglés.

### 2026-11-20 (2): Malla > Triangular y Alinear Normales

**Nuevo**
- **Triangular** (Malla): cada polígono de más de 3 lados pasa a triángulos (abanico). Con selección de polígonos actúa sobre ellos; sin selección, sobre toda la malla.
- **Alinear Normales** (Malla, `U~A`): dentro de cada isla conectada, todos los polígonos quedan orientados igual (invierte los que están al revés). Si ya está todo coherente, lo avisa y no cambia nada.

**Probado:** `iron3d_tests` (cubo: 6 cuadriláteros → triángulos con 8 puntos y malla cerrada; cara invertida detectada y corregida; malla coherente sin cambios). **No probado:** el clic en el menú con la interfaz abierta.
**Nota:** la lista de «huecos de menú» del vault buscaba etiquetas en inglés y Iron3D está en español, así que muchas entradas que da por faltantes (p. ej. casi todo el menú Animación) sí existen. Es una guía aproximada, no un inventario.

### 2026-11-20: presets de interfaz por versión de Cinema 4D

**Nuevo**
- Seis presets integrados en Preferencias → Interfaz: **Cinema 4D R16, R19, R20, S24, 2023** y **R16–R20 (claro)**. Los colores (fondo de paneles, campos, texto y selección) salen de los `dark.col` y `light.col` reales de cada instalación, no de capturas. R16 y R19 comparten colores; S24 y 2023 son más oscuros; el tema claro es el mismo de R16 a R20.

**Probado:** compila; capturas sin input de R19, 2023 y claro (los colores cambian y el contorno de selección sigue al preset).
**No hecho:** los menús y la disposición de ventanas por versión todavía no cambian (solo los colores). Faltan capturas de R16, R20 y S24.

### 2026-11-19 (9): Nulos con forma, orientación «Pantalla»

**Nuevo**
- Las formas planas de un Nulo (círculo, rectángulo, rombo, triángulo, pentágono, hexágono, octógono, estrella) con orientación **Pantalla** ahora **miran a la cámara activa**, como en C4D, en vez de dibujarse fijas en XY. También al elegirlas con el clic. El tamaño sale de la escala X del nodo (la escala no uniforme no se aplica en este modo).

**Probado:** `skin_test` (círculo en el plano perpendicular a la vista, mismo radio, clic en el borde, orientación XY fija sin cambios).

**Sin probar:** en pantalla con un rig real, y con cuatro vistas (F5): las líneas se calculan una vez por fotograma con la cámara activa, así que en las otras tres vistas la forma mira a la activa, no a la suya.

### 2026-11-19 (8): Piel en vivo y formas de los Nulos

**Nuevo**
- **Piel en vivo:** una malla con Piel importada de un .c4d/.lib4d guarda los pesos por vértice (hasta 4 articulaciones) y **sigue a las articulaciones** al moverlas, girarlas, animarlas o resolver la IK (skinning lineal; lo que no suma peso 1 queda rígido). La pose del archivo es la pose de bind. Si editas la malla, la Piel se descarta. Código en `core/Skin.*`.
- **Formas de los Nulos** (círculo, rectángulo, rombo, triángulo, pentágono, hexágono, octógono, estrella, ejes, cubo, pirámide, esfera; radio, proporción y orientación, ids 1000-1003 de `onull.h`): se importan del .c4d, se guardan en .iron y se dibujan en el visor. Son los controladores típicos de un rig con IK. Código en `core/NullShapes.*`.

**Probado:** `skin_test` (rotación de hueso, pesos parciales, caché, malla editada, articulación inexistente, formas de Nulo, visibilidad), roundtrip .iron de la forma, los 7 tests en verde.

**Sin probar:** Piel y formas con un .lib4d real en pantalla (el volcado `--dump-scene` no carga .lib4d); la orientación "Pantalla" se dibuja como XY; los Nulos con forma Punto/Ninguno no se dibujan; las Pieles no se guardan en .iron (al guardar queda la malla deformada); los morphs de Pose Morph no se evalúan; no se puede seleccionar un Nulo pinchando su forma en el visor (solo desde el Gestor de Objetos).

### 2026-11-19 (7): Límites de rotación de las articulaciones

**Nuevo**
- Las articulaciones de un .c4d traen sus **límites de rotación** (H/P/B, mínimo, máximo y fuerza; ids 3009-3015 de `ocajoint.h`, leídos del SDK de R19). Se guardan en .iron y se copian al duplicar.
- **Resolver IK** recorta cada articulación de la cadena a sus límites tras resolver.

**Probado**: recorte por eje, ida y vuelta .iron, duplicado (`ik_test`, `obj_test`). **Sin probar**: que los ids se lean bien en un .c4d real con límites (salen del SDK, no medidos en un archivo), ni el Límite como etiqueta suelta de C4D (otro id, sin medir). Tras recortar, la punta puede quedarse corta del objetivo: no se vuelve a iterar.

### 2026-11-19 (6): glTF 2.0 (.glb / .gltf)

**Nuevo**
- **Archivo > Abrir / Fusionar** acepta `.glb` y `.gltf` (jerarquía con sus transformaciones, mallas de triángulos con normales/UV/color de vértice, varias primitivas por malla con su material, materiales PBR básicos: color base, metálico, rugosidad, emisión, alfa y textura de color externa; buffers incrustados, base64 y externos).
- **Archivo > Exportar > glTF binario (*.glb)**: mallas y primitivas con su transformación de mundo, materiales PBR y ruta de la textura de color.
- Lector de JSON propio: sin dependencias nuevas ni Cineware.

**Probado**: `gltf_test` (ida y vuelta .glb con posición y material, un .gltf escrito a mano con buffer base64 y normales calculadas, archivos rotos). **Sin probar**: archivos reales de otras herramientas (Blender, etc.), texturas incrustadas en el .glb (se avisa, no se extraen), pieles, animaciones, cámaras y luces de glTF (no se importan), ni la GUI.

### 2026-11-19 (5): Solver IK

**Nuevo**
- **Personaje > Resolver IK (experimental)**: para cada etiqueta IK activa con Punta y Objetivo, mueve la cadena (FABRIK: conserva la longitud de cada hueso) para que la punta llegue al objetivo; con Polo, el codo/rodilla se dobla hacia él. Si el objetivo queda fuera de alcance la cadena se estira recta hacia él. Se puede deshacer.
- Menú **Personaje** nuevo (Articulación, Piel, Pesos y Restricción aparecen desactivados: son lo siguiente).

**Probado**: `ik_test` (cadena de 3 huesos alcanzable, fuera de alcance, polo a ambos lados, cadena bajo una base girada y desplazada, etiqueta IK con rutas por nombre y objetivo que se mueve, etiqueta desactivada). **Sin probar**: contra el resultado real de C4D en un rig importado (los límites de giro, el solver 2D y el peso preferido de C4D todavía no se aplican; se resuelve a mano desde el menú, no en cada fotograma), ni en la GUI.

### 2026-11-19 (4): Rig, primer paso: articulaciones y etiqueta IK

**Nuevo**
- Las **articulaciones** de C4D se marcan como tales (`isJoint`) al importar, aunque siguen viéndose como Nulo.
- La **etiqueta IK** de C4D (tipo 1019561) se importa con su estado (activada), solver (2D/3D) y los enlaces a Punta, Objetivo y Polo (por ruta de nombres). Se guarda en `.iron` (líneas `JOINT`/`IK`) y se conserva al duplicar.
- Diagnóstico: `IRON3D_DUMP_TAGS=1` imprime las etiquetas de cada objeto al importar un `.c4d`.

**Probado**: `obj_test` (guardar/abrir/duplicar articulación e IK), los otros tres tests, e importación real de `MODELOS DONNIE.c4d` (aparecen las 6 etiquetas IK). **No hecho todavía**: resolver la IK (mover el objetivo no mueve la cadena), exportar la etiqueta a `.c4d`, y la etiqueta/ajustes de Límite (no estaba en ningún modelo de prueba, su ID sigue sin medir).

### 2026-11-19 (3): Puente y Comprobar Malla

**Nuevo**
- **Malla > Puente**: une dos polígonos con el mismo número de lados con un anillo de cuadriláteros (elige dirección y giro con la menor distancia) y quita los dos originales.
- **Malla > Comprobar Malla**: cuenta aristas abiertas, aristas no manifold, puntos sueltos y polígonos sin área, y selecciona los problemáticos del modo activo.

**Probado**: `c4d_geometry` (cubo limpio, cubo sin tapa = 4 aristas abiertas, puente entre las dos tapas = tubo de 4 cuadriláteros y 8 aristas abiertas). **Sin probar**: en la GUI con ratón, polígonos de más de 4 lados con giros muy distintos.

### 2026-11-19 (2): Wavefront OBJ y estilo de iconos plano

**Nuevo**
- **Importar y exportar OBJ + MTL** (Archivo > Abrir/Fusionar y Exportar > Wavefront; también `Iron3DStudio.exe archivo.obj`). Funciona sin Cineware, también en Linux. Importa polígonos de n lados (se guardan como tales), UV (con la v invertida a la convención de C4D), normales del archivo o suaves calculadas, un objeto por `o`/`g`, y materiales del `.mtl` (color, brillo, emisión, opacidad, textura de Color) con partes por material. Exporta con la transformación de mundo, un `.mtl` aparte y los polígonos originales.
- Preset **Blender**: iconos con **estilo plano propio** (`IconStyle::Flat`: trazo más grueso y colores atenuados; los mismos dibujos de Iron3D, no los de Blender) y espacios de trabajo con sus nombres (Layout, Modeling, Sculpting, UV Editing, Texture Paint, Animation).

**Probado**: `obj_test` (importar, ida y vuelta, material por partes, archivo roto), compilación Debug, apertura de un OBJ de prueba por línea de comandos y captura del preset Blender. **Sin probar**: OBJ grandes (rendimiento), texturas relativas en subcarpetas con la GUI, Linux; los espacios de trabajo de Blender todavía solo cambian la etiqueta.

### 2026-11-19: Preset «Iron3D» (el aspecto de la web) como interfaz predeterminada

**Nuevo**
- Preset de interfaz **Iron3D**: fondo casi negro, paneles #23272B, acento verde #9CD86A y esquinas redondeadas, como staticforge.me/i3ds.html. Es el predeterminado en instalaciones nuevas; Cinema 4D, Blender, Maya, 3ds Max y Source Filmmaker siguen como presets.
- El preset **Blender** toma ahora sus colores del tema oscuro oficial de Blender 4.5 (solo colores y proporciones, ningún código).

**Corregido**: «Restablecer» y el cambio de disposición ya no dependen del orden de la lista de presets.

**Probado**: compila (Debug), `iron3d_tests` y `preset_library_test` pasan, captura del visor con `--ui-preset=iron3d`. **Sin probar**: Release, Linux, y los demás presets a ojo tras el cambio de orden.

### 2026-11-18: Shader Fresnel por píxel en el canal Color (Vulkan y OpenGL)

**Nuevo**
- El shader **Fresnel** de C4D (el rosa de pecho, cinturón y almohadillas de Toy Foxy) ya se
  importa y se evalúa **por píxel** en el visor: color = degradado en `abs(dot(N, V))`, la misma
  fórmula que el GLSL del visor de R19 (cadena embebida en `corelibs/sla.cdl64`). Se hornea el
  degradado a una tira de 256x1 que el shader muestrea según el ángulo de visión (flag
  `kLitTexFresnel`), así que reutiliza el descriptor de textura de Color: no hay pipeline nuevo.
- Modo físico (IOR + Invertir) aproximado con Schlick. Color, brillo y mezcla del canal
  (Normal/Sumar/Restar/Multiplicar) se aplican al hornear.
- Volcado de parámetros crudos de shaders sin evaluar: `IRON3D_DUMP_SHADERS=1`.
- Prueba sin GPU `fresnel_test` (tira 256x1, modo físico, Multiplicar, clave de caché, ida y
  vuelta del `.iron`).

**Probado**: compila, `iron3d_tests` y `fresnel_test` pasan; Toy Foxy importa el Fresnel como
raíz del canal Color en sus 4 materiales (antes salía como «Otro», gris).

**Corregido tras comparar con R19 (2026-11-18)**: la primera versión dejaba el Fresnel sin
aplicar en los objetos cuya etiqueta de textura no usa proyección UVW (el cinturón usa otra), así
que pecho, cinturón, nariz, orejas y almohadillas salían blancos. Ahora el Fresnel se aplica con
cualquier proyección. Contrastado a ojo con R19 (viewport del GUI y render de `Commandline.exe`,
capturas en el vault `14-Iron3D-CoalMix/comparaciones_r19/`): el reparto del rosa coincide.
Flags de verificación nuevos: `IRON3D_NO_IMPORT_REPORT=1` (oculta el aviso de importación al
capturar) y línea `Fresnel:` con el degradado en `--dump-scene`.

**Diferencias que quedan frente a R19** (sin resolver): (1) en Iron3D las siluetas de las piezas
blancas tienen un borde marrón (el nudo oscuro del degradado) que en R19 apenas se ve; la
interpolación/curva del degradado puede no ser la de R19. (2) La pajarita sale rosa pálido y en R19
magenta oscuro: probablemente un objeto con varias etiquetas de textura donde Iron3D elige otra.
(3) El cuerpo se ve más brillante que en R19 (iluminación distinta, no del Fresnel).

**Sin probar / límites**: contraste solo visual, no píxel a píxel (cámaras distintas). Solo vale como **raíz del canal Color**: un Fresnel dentro de
una Capa o en otro canal no es por píxel (en Capa se toma de cara, |N·V| = 1; en otros canales
se ignora). El modo Render se lee pero no se aplica: «frontal»/«trasera»/«transparentes» se
tratan como «ambas caras». La opción «Usar relieve» tampoco. Los miniaturas del navegador (CPU)
no lo evalúan.

### 2026-11-18: Transparencia y Alfa del material en el visor (Vulkan y OpenGL)

**Nuevo**
- **Transparencia**: un material con el canal Transparencia activo se ve translúcido
  (opacidad = 1 menos la luminancia de color x brillo). Se dibuja en una segunda pasada,
  tras lo opaco, con mezcla por alfa y sin escribir profundidad. En Vulkan es un pipeline
  aparte; en OpenGL, `glBlendFunc` + `glDepthMask`.
- **Alfa**: el canal Alfa (con Invertir y "Alfa de la imagen") se funde en el canal A de la
  textura de Color y el shader descarta los píxeles con alfa < 0,5 (recorte, sin mezcla).
- `--dump-scene` imprime una línea por material con Transparencia o Alfa, y
  `--load-preset=?` lista los modelos de una `.lib4d` (solo verificación).

**Probado**
- Compila en Debug; los tres tests (`iron3d_tests`, `preset_library_test`, `c4d_roundtrip_test`) pasan.
- Escena sintética (esfera con Transparencia 0,6 y plano con Alfa de imagen con agujeros):
  captura PrintWindow en Vulkan y en OpenGL, ambas correctas.

**Sin probar / límites**
- Ninguno de los modelos reales que revisé (Toy Foxy, Elizabeth Afton, Crying Child y los
  mapas de FNaF, entre otros) usa Transparencia ni Alfa, así que no está contrastado contra Cinema 4D R19 con un caso real.
- Los objetos transparentes no se ordenan por distancia entre sí (se mezclan en el orden de
  la escena). El Alfa es recorte duro: sin borde suave ("Alfa suave") ni color de alfa.
- La Transparencia no aplica refracción, absorción ni el brillo de Fresnel de R19.

### 2026-11-11 (17): Simetría y Subdivision Surface al importar, Banking corregido y blancos que salían negros

**Nuevo**
- **Generadores Simetría y Subdivision Surface sin caché**: las `.lib4d` no guardan la malla
  generada, y estos dos salían como Nulo con el hijo en crudo (medio torso, mandíbula y
  cintura a la mitad, piezas en bloque). Ahora se evalúan al importar sobre su primer hijo,
  también encadenados (SDS > Simetría > Cubo). Simetría lee el plano, Soldar y la
  Tolerancia; sobre un Nulo refleja toda su jerarquía y cada pieza conserva su material.
  SDS usa Catmull-Clark con el nivel del editor (el que enseña el visor de C4D), y las UV y
  los materiales por Selección de Polígonos pasan a las caras hijas. El objeto de entrada
  queda oculto, como en C4D.
- `--dump-scene` también enseña la transformación local, las partes por selección, cada
  material con sus capas de Reflectancia y el shader del canal Color.

**Corregido**
- **Signo del Banking (B) al importar y exportar**: medido contra la matriz que guarda C4D
  en los 97 objetos girados de Toy Foxy, solo (P, H, -B) la reproduce. Todo lo que tenía
  Banking salía girado al revés: la oreja izquierda de Toy Foxy flotaba lejos de la cabeza y
  los brazos quedaban en otra pose. Corregido en las pistas de animación y en el
  exportador. Los `.c4d` exportados antes con Banking salen con ese giro invertido.
- **Capas de Reflectancia sin Fresnel al 100 % con el canal Color activo**: se convertían en
  cromo y, sin entorno que reflejar, el objeto salía negro (el cuerpo blanco de Toy Foxy).
  El visor de R19 sigue enseñando el color y la capa solo suma el brillo de las luces
  (comprobado en el GLSL del visor que trae R19); ahora Iron3D hace lo mismo.

**Probado**: tests `c4d_geometry`, `c4d_roundtrip` y `preset_library` en verde; Toy Foxy de
`fnaf 2 toys animatronics by alfred.lib4d` comparado por captura con R19 (pose, orejas y
cuerpo blanco y suave).

**Sin probar / pendiente**
- Toy Chica y el resto de modelos de control tras el cambio de Banking (la captura no se pudo
  hacer en esta sesión: la ventana abría minimizada).
- El shader **Fresnel** de C4D (rosa de Toy Foxy en pecho, cinturón y almohadillas) todavía no
  se evalúa: sale blanco. R19 lo hace por píxel en el visor con `abs(dot(N, V))` sobre el
  degradado.
- Simetría sobre un Recorrido NURBS u otros generadores de splines (Iron3D no tiene splines).
- UV de la SDS interpoladas linealmente (C4D las suaviza con el modo Estándar).

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

### 2026-11-19 (9): Seleccion de Nulos pinchando su forma
- Un Nulo con forma (controlador de rig) se elige con clic sobre sus lineas en el visor (core/NullShapes: rayHitsNullShape; tolerancia ~1.2% de la distancia). Probado por test (borde, centro vacio, fuera, Ninguno).
- Sin probar en pantalla con un rig real.

### 2026-11-19 (10): IK en vivo
- La IK se resuelve cada fotograma (antes solo a mano desde el menu Personaje): al mover el controlador la cadena lo sigue y la Piel acompana. Una etiqueta IK desactivada no actua. Compila y ik_test/skin_test pasan; sin probar en pantalla con un rig real ni su coste con rigs grandes.

### 2026-11-19 (11): Simetria de edicion (estilo True Symmetry)
- Malla > Simetria de edicion: plano X/Y/Z local del objeto y tolerancia en cm. Al mover puntos/aristas/poligonos con el gizmo, los puntos espejo sin seleccionar se mueven reflejados y los que estan sobre el plano no lo abandonan. Compila; SIN probar en pantalla (no hay test automatico del arrastre). No cubre aun Extruir/Bisel/Cuchillo ni escultura, ni simetria radial.

### 2026-11-19 (12): Objeto Simetria (generador, como el de C4D)
- Modelado > Simetria crea un Nulo con simetria (si hay un objeto con malla seleccionado, lo mete dentro). El nodo dibuja la copia reflejada de su primer hijo respecto a un plano del propio nodo (X/Y/Z, tolerancia en cm en Atributos); el hijo se queda editable y visible, y cualquier cambio suyo se refleja al instante. Se guarda en .iron (linea SYM) y se duplica. Test en skin_test (reflejo, giro de triangulos, normales, cache, plano, tolerancia) y captura en pantalla (rejilla con bulto a un lado: sale simetrica). No hay soldado real de puntos (las dos mitades se tocan pero son mallas separadas), ni exporta a .c4d como generador, ni admite varios hijos. Dos simetrias en total: esta (objeto) y la de edicion (Malla > Simetria de edicion).
- Flags de verificacion: --ui-cmd=sym=x|y|z|off, comp-move=dx,dy,dz y sym-obj.
