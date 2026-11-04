# Oclusión ambiental y colores a tu gusto

*3 de noviembre de 2026 · Static Forge*

Dos cosas pequeñas en tamaño de código y grandes en cómo se ve el programa.

## SSAO: rincones que se oscurecen solos

Un cubo apoyado en un plano sin oclusión ambiental parece pegado con celo: la luz llega
igual al centro de la cara que a la esquina donde toca el suelo. La **oclusión ambiental
en espacio de pantalla** (SSAO) lo corrige oscureciendo los huecos y los contactos.

Lo hemos montado en tres pasadas propias del backend Vulkan:

1. un **G-buffer** con la posición y la normal de cada píxel;
2. el **SSAO**, con 16 muestras en un hemisferio orientado por la normal (espiral de
   ángulo dorado y rotación por píxel con un hash, sin textura de ruido);
3. un **desenfoque** de caja de 4x4 para quitar el grano.

Solo atenúa el término ambiente del shader, nunca la luz directa. Así no ensucia las
caras iluminadas. El programa sigue entre 600 y 950 FPS con las tres pasadas activas.

**Pendiente**: una comparación visual a fondo, con un objeto apoyado en un plano, que
la automatización de esta sesión no permitió hacer bien. El detalle técnico está en
[`ROADMAP_RENDER.md`](../../ROADMAP_RENDER.md).

## Colores de la interfaz

En Preferencias > Interfaz se pueden cambiar unos pocos colores "semilla": fondo de
paneles, campos, acento, azul de herramientas y texto. El resto de la paleta (bordes,
hovers, barras, pestañas...) se deriva de ellos, así que cambiando cinco colores no se
rompe nada. El tema por defecto es literalmente el de siempre: la esencia de Cinema 4D
se conserva por construcción, no por aproximación.

Esto es la base de lo que vino al día siguiente: [los presets de
interfaz](2026-11-04-iconos-y-presets.md).
