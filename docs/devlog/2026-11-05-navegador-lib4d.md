# El Navegador de Contenido, y cómo leer un `.lib4d` sin Cinema 4D

*2026-11-05*

En Cinema 4D, quien publica modelos casi nunca los comparte como `.c4d` sueltos. Los
comparte como **librería de preestablecidos** (`.lib4d`): un solo archivo con los modelos,
sus texturas y una miniatura de cada uno. Arrastras el archivo a C4D, aparece en el
Navegador de Contenido, bajo *Preestablecidos*, y desde ahí cargas lo que quieras. Si lo
copias a la carpeta `library/browser`, sale siempre.

Iron3D ya hace lo mismo.

## Qué hay

- Un **Navegador de Contenido** en el hueco del Gestor de Objetos, con su pestaña
  vertical, como en R19. A la izquierda, el árbol: Catálogos, Elementos Recientes,
  Favoritos, Ordenador, Preestablecidos, Red y Resultado de la Búsqueda. A la derecha, la
  rejilla con las miniaturas.
- **Abrir un `.lib4d`** (Archivo > Abrir, o pasarlo al ejecutable) lo monta en
  Preestablecidos para esta sesión y abre el navegador ahí. No toca la escena.
- **Las de `library/browser/`** (junto a `Iron3DStudio.exe`) se montan al arrancar.
  *Hacer Permanente* copia ahí una temporal.
- **Doble clic en un modelo**, o arrastrarlo al visor, lo añade a la escena. Ctrl+Z lo
  quita.

## Cómo es un `.lib4d` por dentro

No había documentación pública del formato, así que lo deduje de los archivos. Solo miré
entrada y salida: los `.lib4d` que tengo y lo que C4D R19 saca de ellos con el plugin
extractor de la sesión anterior. No hay código de Maxon en Iron3D.

```
"dbfh" + 12 bytes                      cabecera
[etiqueta 4][id u32 BE][tamaño u64 BE][cuerpo]   ... bloques ...
"dbfe"                                 fin
```

| Etiqueta | Qué guarda |
|---|---|
| `difm` | Cabecera de la base: `preset://<librería>.lib4d` y el título. |
| `ditm` | Un elemento: `preset://<librería>.lib4d/<ruta>`, su nombre visible y su tipo. |
| `bina` | Los datos del elemento. Pueden ir tal cual (un PNG) o con 4 bytes de tamaño seguidos de un flujo zlib. En los modelos, al descomprimir sale un `.c4d` completo. |
| `imag` | La miniatura, un JPEG. |
| `ivld` | Un registro anulado. Se ignora. |

Dos detalles que costaron:

1. **C4D no reescribe la librería al editarla: añade bloques nuevos al final.** El mismo
   id puede aparecer varias veces, y vale el último.
2. **El tipo del elemento** (1018107 = documento/modelo) está en la clave 1 del
   contenedor, pero el registro tiene *otra* clave 1 antes del nombre que no es el tipo.
   Hay que leer la que va después del nombre.

Con eso, todas las librerías que tengo se leen bien, incluida una de 900 MB con 356
elementos. El archivo nunca se carga entero en memoria: el lector recorre las cabeceras y
solo lee un modelo cuando lo cargas.

## Cómo entra un modelo en la escena

El modelo es un `.c4d`, así que no hace falta un importador nuevo:

1. Se descomprime el bloque `bina` (zlib de stb_image).
2. Se escribe en un archivo temporal.
3. Pasa por el importador `.c4d` de siempre (Cineware), en modo fusionar.
4. Se borra el temporal.

Leer la librería funciona en cualquier sistema. Cargar el modelo necesita Cineware, que
solo existe en Windows.

## Lo que falta

- **Crear tus propias librerías** (*Nueva Librería de Preestablecidos*, guardar un
  objeto como preestablecido). Para eso hay que escribir `.lib4d` que C4D acepte. La idea
  es comprobarlo con el propio C4D R19 y el plugin extractor como juez.
- **Texturas**: el navegador las enseña, pero los materiales de Iron3D todavía no las
  usan.
- Catálogos, Favoritos, Ordenador y Red, que de momento solo ocupan su sitio en el árbol.
- Probar con el ratón lo que de momento solo está probado por captura: arrastrar al
  visor, menús y búsqueda.

Después de esto toca la parte de Blender: imitar también su forma de trabajar y abrir
`.blend`. Siempre con código propio, porque el de Blender es GPL.
