# Guardar en `.c4d`, y tus objetos como preestablecidos con miniatura

*2026-11-05*

Iron3D ya abría `.c4d` y librerías `.lib4d`. Ahora también **guarda** en `.c4d`, y con
eso llega lo que faltaba en el Navegador de Contenido: hacer clic derecho en un objeto de
la escena y guardarlo como preestablecido, con su miniatura.

## Exportar a Cinema 4D

Archivo > Exportar > **Cinema 4D (*.c4d)** guarda toda la escena. **Guardar Objetos
Seleccionados como...** guarda solo lo seleccionado. Se hace con el Cineware SDK de Maxon,
el mismo que ya se usaba para importar.

Lo que hay en Iron3D llega a C4D así:

| En Iron3D | En Cinema 4D |
|---|---|
| Cubo, Esfera, Plano | La primitiva de C4D, con sus parámetros (se puede seguir editando) |
| Malla | Objeto Poligonal |
| Luz | Luz |
| Nulo | Nulo |
| Material | Material estándar: color y un reflejo GGX con la rugosidad; metal = Fresnel de conductor |
| Material asignado | Etiqueta de textura, solo donde cambia respecto al padre (C4D lo hereda) |

### Las mallas, al revés

Una malla de Iron3D está pensada para la GPU. Cada punto se repite en todas las caras que
lo tocan, y todo son triángulos. C4D quiere lo contrario: puntos únicos y cuadriláteros.
Así que, al exportar:

1. Se unen los vértices que están en la misma posición.
2. Cada pareja de triángulos seguidos que comparten arista, están en el mismo plano y
   forman un cuadrilátero convexo se convierte en un solo cuadrilátero. Un cubo vuelve a
   tener 6 caras y no 12 triángulos.
3. Si las normales estaban suavizadas, el objeto lleva una etiqueta Phong.

Hubo una sorpresa. El cubo propio de Iron3D tiene algunas caras con el giro al revés.
Nunca se había notado, porque Iron3D dibuja las dos caras de cada triángulo. Al exportar
manda la normal de los vértices, que es la que se ve iluminada, y no el orden de los
puntos.

### Las coordenadas, al revés

Al importar ya hacía falta un espejo en Z: C4D usa ejes zurdos en centímetros, e Iron3D
ejes diestros en unidades de 2 m. Exportar es la operación inversa. Para comprobarla, una
prueba automática pasa la transformación por ida y vuelta y verifica que se obtiene la
misma matriz, incluidas rotaciones compuestas, escalas distintas por eje y un espejo.

## Guardar como Preestablecido

Clic derecho en un objeto > **Guardar como Preestablecido...** abre una ventana con la
miniatura del objeto. En ella eliges la librería, la carpeta y el nombre, y le das a
Guardar. El objeto se guarda con sus hijos y con los materiales que usan. Luego se puede
arrastrar a otra escena, en Iron3D o en C4D.

## Las miniaturas

C4D renderiza una miniatura de cada preestablecido. Iron3D lo hace con un
**rasterizador propio por CPU**: no usa la GPU, así que funciona igual dentro de la app,
en las herramientas de consola y en las pruebas automáticas de Linux.

- La vista es desde arriba a la derecha, con perspectiva.
- Hay una luz principal, una de relleno y un poco de contorno, con el color, la rugosidad
  y el metal de cada material.
- Se dibuja al doble de tamaño y se reduce, así los bordes quedan suaves.
- El encuadre se ajusta a lo que el objeto ocupa en pantalla. Con la esfera envolvente,
  un personaje con un objeto suelto lejos salía pequeño.

En la `.lib4d` se guarda en JPEG de 256×256, en un bloque `imag` con el mismo número que
el elemento. La clave 5 del registro apunta a ese bloque, como en las librerías de C4D.
Al añadir un `.c4d` con "Añadir Archivos", también se importa y se le hace su miniatura.

## Validado con Cinema 4D

Se usa el mismo juez que en la nota anterior: C4D R19 en modo consola, sin ventana.

- Tres `.c4d` exportados por Iron3D: R19 los abre y los vuelve a guardar. Uno era un
  personaje de 820 000 polígonos y 14 materiales. Al leer lo que guardó R19, salen los
  mismos objetos, posiciones, materiales y número de polígonos.
- Una librería con 13 preestablecidos y sus miniaturas, escrita entera por Iron3D:
  R19 abre los 13.

## Lo que falta

- **El ID de aplicación es provisional.** Cada `.c4d` guarda el número de la aplicación
  que lo escribió. Iron3D escribe 1000001 hasta que Static Forge registre el suyo en
  developers.maxon.net.
- Falta ver con la ventana de C4D abierta si enseña nuestras miniaturas. R19 sin ventana
  acepta la librería, pero no las dibuja.
- Las cámaras todavía no se exportan, porque Iron3D aún no tiene cámaras como objeto.
- Lo interactivo (menús, la ventana de preestablecido y el diálogo "Guardar como") se ha
  probado solo por captura, no con el ratón.
