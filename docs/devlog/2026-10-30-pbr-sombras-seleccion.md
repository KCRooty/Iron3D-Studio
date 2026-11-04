# Materiales PBR, sombras y selección múltiple

*30 de octubre de 2026 · Static Forge*

La gente del Discord con la que compartimos Iron3D nos lo dijo claro: el render tenía
que parecer de verdad, no un visor plano. Esta tanda va de eso, y también de que el
programa se maneje más como un DCC de verdad.

## Render

- **PBR** en el backend Vulkan: Cook-Torrance con GGX, Smith y Fresnel-Schlick, y
  materiales con **metálico** y **rugosidad** en vez de especular/brillo. El Editor de
  Material tiene los dos deslizadores, y las bolas de previo los imitan.
- **Luz de ambiente de hemisferio**: un cielo y un suelo de color, muestreados por la
  normal y por la reflexión. Sin ella, un material casi espejo se veía negro fuera del
  brillo especular, que es lo físicamente correcto cuando no hay nada que reflejar
  pero no sirve para trabajar. No es IBL de verdad (eso pide un HDRI y un cubemap), es
  un apaño honesto hasta entonces.
- **Sombras** del primer foco de la escena: shadow map ortográfico con PCF 3x3. Al
  probarlo en vivo no se veía ninguna sombra aunque todo compilaba: el volumen de la
  luz estaba en centímetros y el motor trabaja en unidades de escena, así que era 200
  veces más grande que la escena. Una vez corregido se probó con un cubo sobre un
  plano.
- OpenGL se queda en Phong a propósito: el render bueno va por Vulkan.

El formato `.iron` guarda ahora `metallic`/`roughness`, sin migración de los archivos
viejos.

## Manejo

- **Selección múltiple**: Mayús+clic en el visor y Ctrl+clic en el Gestor de Objetos.
  Todo lo seleccionado sale con el nombre y el contorno en naranja, y Supr lo borra
  todo.
- El **gizmo se pone en el centro de la selección**, como en Cinema 4D. Al arrastrarlo,
  el resto de objetos copia la rotación y la escala y orbita alrededor de ese centro.
- **Modos libres del gizmo**: al arrastrar fuera de los aros de Rotar, el objeto gira
  en el plano de la vista; por dentro, rotación libre tipo trackball (el arcball de
  Ken Shoemake, un algoritmo público de 1992). Al arrastrar fuera de los ejes, Mover
  desplaza en el plano de vista y Escalar escala de forma uniforme.

**Pendiente**: probar a fondo los modos libres del gizmo y la selección múltiple con el
ratón.
