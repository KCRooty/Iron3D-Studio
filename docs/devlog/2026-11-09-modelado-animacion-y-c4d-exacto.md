# Editar la malla de verdad, animar con curvas, y un `.c4d` que va y vuelve igual

*2026-11-09*

Hasta esta tanda Iron3D colocaba objetos, pero no los modelaba: no se podía tocar un punto,
una arista o una cara. Tampoco se podía animar más allá de grabar claves, y un `.c4d`
exportado perdía las texturas, las cámaras y las UV por el camino. Las tres cosas han
cambiado entre el 7 y el 9 de noviembre.

## Puntos, aristas y polígonos

Los botones de la barra izquierda (Modelo, Puntos, Aristas, Polígonos) ya cambian de modo
sobre un objeto editable (C > Hacer Editable). Lo que se ve y se elige es lo que esperas
de C4D:

- **Polígonos de cuatro lados de verdad.** Un cubo son 8 puntos, 12 aristas y 6 caras, no
  12 triángulos. La topología vive en el núcleo y el `.iron` 6 la guarda.
- **Lo que está detrás no se ve ni se elige**, con un z-buffer en CPU. *Solo Seleccionar
  Visibles* lo desactiva para coger lo de atrás.
- **Seleccionar con cualquier herramienta.** Esto era un fallo que encontró Core: solo la
  herramienta de selección elegía caras, porque Mover, Rotar y Escalar convertían
  cualquier clic fuera del gizmo en un arrastre libre. Ahora el gizmo solo se queda el clic
  si cae en una flecha o un aro; si no, el clic elige lo que hay debajo (Mayús añade, Ctrl
  quita) y, si arrastras, lo mueve en el momento. Hasta pasar de 3 píxeles no se mueve nada
  ni se crea un paso de deshacer. Lo mismo vale para los objetos en modo Modelo.
- **Herramientas:** Extrusión (D), Extrusión Interna (I), Bisel de polígonos, Cuchillo en
  línea (K), Selección de Bucle y de Anillo, Aumentar, Reducir, Invertir, Seleccionar
  Conectados, Soldar, Cerrar Agujero, Subdividir e Invertir Normales. Todo se deshace.

Cada operación tiene su test con números: extruir la cara de arriba del cubo da 12 puntos,
20 aristas y 10 caras, y la malla sigue cerrada (V − A + C = 2).

## Claves y curvas

Las claves de la Línea de Tiempo se eligen, se arrastran y se borran. Cada clave lleva su
interpolación (Spline, Lineal o Escalón), que manda en el tramo que empieza en ella, como en
C4D. El nuevo **Editor de Curvas** dibuja cada pista con sus tres componentes.

## Un `.c4d` que vuelve igual

La meta era que lo que sale de Iron3D, pasado por Cinema 4D y vuelto a abrir, tuviera los
mismos números. Ahora van y vuelven:

- los materiales con todos sus canales y texturas, y las capas de Reflectancia en su orden;
- las etiquetas de textura y las UV de cada esquina de cada polígono (antes se perdían);
- los diez tipos de luz de C4D y las cámaras, con su objetivo;
- posición, HPB y escala exactas, la visibilidad y las pistas de animación, con su
  interpolación.

La prueba es un bucle cerrado. Iron3D exporta una escena con tres materiales, seis texturas,
cuatro luces, dos cámaras, un grupo oculto, una malla con UV y un cubo animado. La metemos
en una `.lib4d` y Cinema 4D R19, sin ventana, la abre y la vuelve a guardar. Iron3D
reimporta lo que guardó R19 y lo compara con el original: todo coincide con una tolerancia
de 1e-4, y las curvas de animación difieren como mucho cuatro diezmilésimas de grado.

Por el camino salieron tres cosas que no se veían a simple vista:

- R19 añade `./` delante de las rutas de textura al guardar.
- Las capas de reflejo se leían al revés.
- Con las tangentes automáticas de C4D la curva entre claves no era la de Iron3D. El
  exportador escribe ahora las asas a mano; medimos que C4D trata como neutra un asa de un
  cuarto del tramo, no de un tercio.

Las librerías `.lib4d` también se cerraron: renombrar, mover y eliminar elementos y
carpetas, un lector que sigue la estructura real del formato en vez de suponer un orden de
bloques, y «Corregir Url Interna» para las librerías que C4D ignora si se cambia el nombre
del archivo.

## Lo que falta

- **Nada de esto se ha probado todavía con el ratón de una persona**: las capturas de
  verificación no pueden hacer clics. Tampoco con el backend OpenGL.
- El visor Vulkan **no dibuja texturas** todavía. El render por CPU dibuja Color y
  Luminancia; Normal, Relieve, Alfa y Transparencia se guardan y exportan, pero no se ven.
- Las animaciones hechas en C4D llegan con las claves exactas, pero entre claves la curva
  puede separarse hasta un 5 % del recorrido.
- No hay controles para editar luces, cámaras ni los canales nuevos en el Gestor de
  Atributos.
- En el Editor de Curvas las claves todavía no se arrastran. No hay bisel de aristas,
  Puente ni Cuchillo en bucle; Subdividir no suaviza.
- MoGraph necesita antes objetos generadores (los que crean malla a partir de sus hijos).
  Es lo siguiente de la lista, junto con Comprobar Malla y Puente.
