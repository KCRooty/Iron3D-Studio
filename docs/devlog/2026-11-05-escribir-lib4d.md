# Crear tus propias librerías `.lib4d`, y una flecha que te lleva al objeto

*2026-11-05*

Hasta ahora Iron3D solo **leía** librerías de preestablecidos de Cinema 4D. Ahora también
las **escribe**: puedes crear una librería nueva, hacerle carpetas y meterle tus modelos
`.c4d` y tus texturas. Y lo que escribe Iron3D lo abre Cinema 4D R19 sin problema.

De paso, el visor tiene la flechita de C4D que apunta al objeto seleccionado cuando
no lo ves.

## La flecha de "objeto fuera de vista"

Si el objeto seleccionado se queda fuera del visor, detrás de la cámara o tan lejos que
no se ve, sale en el borde una punta de flecha azul clara apuntando hacia él, igual que
en C4D R19. Con un clic, la cámara viaja hasta él en un tercio de segundo y lo encuadra
sin cambiar el ángulo de vista. Si mueves la cámara mientras viaja, el viaje se cancela.

## Escribir un `.lib4d`

El formato ya lo conocíamos por fuera ([cómo se lee](2026-11-05-navegador-lib4d.md)).
Para escribirlo hacía falta entender el interior de cada registro, no solo sacar de él
la url y el nombre.

**Los registros son contenedores serializados.** Cada valor lleva delante un byte que
dice qué es: entero de 32 bits, entero de 64, cadena, contenedor anidado o fin de
contenedor. Esos números son los de la lista pública `HYPERFILEVALUE_*` de la
documentación del SDK de Maxon (solo la lista de números, sin código). Para comprobar que
la gramática estaba bien, se leyeron y se volvieron a escribir los **816 registros** de
cinco librerías reales: los 816 salieron idénticos byte a byte.

Con eso, lo que guarda un modelo es:

| Clave | Qué es |
|---|---|
| 6 | La url (`preset://<librería>.lib4d/<ruta>`) y un número de comprobación. |
| 14 | Cómo van los datos (0 = tal cual, 2 = zlib), su tamaño y **la posición del bloque `bina` dentro del archivo**. |
| 16 | El nombre visible, por idioma. |
| 1 | El tipo (1018107 = documento). |
| 11, 13 | Fecha de creación y de modificación. |
| 5 | Miniaturas, también con su posición en el archivo (opcional). |

Las texturas llevan además ancho, alto, modo de color, si tienen alfa y el perfil de
color.

Tres cosas que no eran evidentes:

1. **El número de comprobación es un CRC-32 de la url en minúsculas, en UTF-16BE.** Se
   probaron varias funciones de hash contra 61 registros reales, y esta acierta en los 61.
2. **Las posiciones son absolutas.** El registro apunta al byte exacto donde empieza su
   bloque de datos, así que no se pueden mover bloques sin reescribir los registros.
3. **La cabecera de cada registro lleva el número de build de C4D**: 19024 es R19.024, y
   las librerías de R21 llevan 21027. Iron3D escribe como R19.024, que es la versión con
   la que se valida.

## Cómo se escribe sin romper nada

Igual que C4D: **nunca se reescribe lo que ya hay.** Para añadir algo, Iron3D quita el
cierre (`dbfe`), escribe al final los bloques nuevos (datos y registros) y vuelve a
poner el cierre. Así añadir un modelo a una librería de 900 MB cuesta lo mismo que
añadirlo a una vacía, y las posiciones de todo lo anterior siguen valiendo.

Los modelos se guardan comprimidos con zlib (el compresor de stb_image_write) y las
texturas tal cual.

## Validado con el propio Cinema 4D

El juez es C4D R19 en modo consola, sin ventana, con el extractor de la sesión anterior:
monta la librería, recorre sus elementos, carga cada modelo como documento y lo vuelve a
guardar. Pasaron las tres pruebas:

- Una librería nueva escrita por Iron3D, con un modelo en la raíz, otro en una
  subcarpeta, una textura y una carpeta vacía. R19 lo encuentra todo y carga los dos
  modelos.
- Una **copia** de una librería real de 19 modelos a la que Iron3D le añade uno más al
  final. R19 carga los 20.
- Una prueba automática (Windows y Linux) que crea, añade y vuelve a leer, y comprueba
  que el CRC-32 da el mismo valor que una librería real.

## En la interfaz

En el Navegador de Contenido:

- **Archivo > Nueva Librería de Preestablecidos...** pide un nombre y crea la librería
  en `library/browser`, así que es permanente.
- **Archivo > Nueva Carpeta...** crea una carpeta dentro de la librería o carpeta
  abierta.
- **Archivo > Añadir Archivos a la Librería...** (o clic derecho en la librería): los
  `.c4d` entran como modelos y el resto (texturas) como archivos.
- **Arrastrar desde el Explorador**: una `.lib4d` soltada en Iron3D se monta, como en
  C4D. Otros archivos soltados encima del navegador se añaden a la librería abierta.
- Las texturas sin miniatura guardada enseñan la propia imagen.

## Lo que falta

- **Guardar un objeto de la escena como preestablecido.** Iron3D todavía no exporta su
  escena a `.c4d`, que es lo que va dentro de cada modelo de la librería.
- **Miniaturas de los modelos que añades:** C4D renderiza una; Iron3D aún no.
- Borrar, renombrar y mover elementos dentro de una librería.
- La hora de creación se guarda con el empaquetado que encaja con los ejemplos reales,
  pero no está confirmada. La fecha sí.
- Lo interactivo (los diálogos, arrastrar desde el Explorador, el clic en la flecha) se
  ha probado solo por captura y con flags de verificación, no con el ratón.
