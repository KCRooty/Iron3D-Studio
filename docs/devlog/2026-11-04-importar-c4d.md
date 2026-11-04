# Iron3D ya abre archivos de Cinema 4D

*4 de noviembre de 2026 · Static Forge*

Iron3D quiere ser el sitio al que llevar los proyectos hechos en Cinema 4D R19. Hasta hoy
eso era una promesa: no había forma de meter un `.c4d` dentro. Ahora sí. **Archivo >
Abrir** acepta `.c4d`, **Archivo > Fusionar** lo añade a la escena que tengas abierta, y
`Iron3DStudio.exe archivo.c4d` lo abre al arrancar.

## Cómo lee un .c4d

Maxon publica un SDK gratuito para leer y escribir `.c4d` sin tener Cinema 4D instalado:
el **Cineware SDK** (antes se llamaba Melange). Iron3D lo usa solo para eso, y todo lo
demás es código nuestro: cómo se recorre la escena, cómo se convierten las coordenadas,
cómo se rehacen las primitivas y cómo se traducen los materiales. El SDK no está en el
repositorio. Quien compila Iron3D lo descarga de Maxon y le indica la carpeta a CMake. Sin
él, Iron3D compila igual y, si intentas abrir un `.c4d`, te explica por qué no puede.

Lo que entra:

- **La jerarquía entera**, con nombres, posición, rotación, escala y visibilidad.
- **Mallas poligonales**, con el suavizado de la etiqueta Phong. Si un deformador ya
  había deformado la malla y C4D guardó el resultado, entra la malla deformada.
- **Primitivas** (cubo, esfera, cilindro, toroide, pirámide...): no se copian, se
  **rehacen** con sus parámetros, igual que hace C4D al abrir el archivo.
- **Generadores** cuya geometría C4D guardó en el archivo: entran como malla, y sus hijos
  quedan ocultos, porque eran las entradas del generador.
- **Materiales**: color, brillo, rugosidad y metal, sacados de la primera capa de
  reflectancia.

Al terminar sale una ventana con el recuento (objetos, mallas, materiales, polígonos) y
una lista de lo que no se pudo traer. Preferimos decirlo claramente a dejar un Nulo
vacío sin explicación.

## Dos sistemas de coordenadas

Cinema 4D trabaja con la mano izquierda, en centímetros. Iron3D, con la mano derecha y en
unidades de 2 metros. Cada punto se invierte en Z y se escala, cada matriz pasa por un
espejo antes de descomponerse en posición, rotación y escala, y cada polígono invierte su
orden de vértices para que las caras sigan mirando hacia fuera. Esto tiene su propio test
automático, el primero del proyecto, y la CI lo ejecuta en Windows y en Linux en cada
push.

## Lo que todavía no entra

Las cámaras, splines, deformadores, objetos de escena (cielo, suelo, entorno) y
generadores sin geometría guardada (Subdivision Surface, Simetría...) entran de momento
como Nulos que conservan su posición. Tampoco hay UV, texturas ni animación. En Linux no
hay importador, porque Maxon solo publica Cineware para Windows y macOS.

Probado con archivos reales: el más grande tiene 121 objetos, 573 368 polígonos y 14
materiales, y carga en 3,6 segundos en una build de depuración. Queda pendiente un
problema: guardar esa escena como `.iron` produce un archivo de 200 MB que tarda casi un
minuto en volver a abrirse. El formato `.iron` es texto y no estaba pensado para mallas
así. Tocará pasarlo a binario.

Lo siguiente: los presets de las bibliotecas `.lib4d` de R19.
