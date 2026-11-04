# Roadmap del motor de render

Contexto: la comunidad de artistas FNAF/C4D con la que se ha compartido el proyecto
espera que el render de Iron3D sea mejor que el de Cinema 4D. Esta nota fija qué
significa eso en la práctica y en qué orden se va a abordar.

## El objetivo real (leído 2026-10-30)

Desde Cinema 4D 2024.0, el renderer por defecto **no es el "Standard"** (scanline,
solo CPU, el que tiene fama de anticuado y es probablemente al que se refiere la
gente del Discord con "una porquería") — es **Redshift**, un path tracer GPU de
producción con años de desarrollo de un equipo dedicado.

- **Fuente:** [help.maxon.net — Redshift Renderer](https://help.maxon.net/c4d/2025/en-us/Content/html/RS_AS_STANDARD_RENDERER.html)
  confirma que Redshift está activado por defecto en Render Settings desde C4D 2024.0.

**Conclusión honesta:** superar el Standard es realista en un plazo razonable con
un renderer híbrido (rasterizador + RT). Superar Redshift no lo es para un
desarrollador solo, en ningún plazo cercano — y no hace falta: lo que la gente
del Discord compara en la práctica es el look plano/sin sombras de un motor sin
pulir, no un path tracer de producción. El objetivo de este roadmap es cerrar esa
brecha visual, no competir con Redshift.

## Estado actual (actualizado 2026-11-03, tras Fase 1, 1.5, 2 y 3)

Vulkan (`LitPipeline`/`lit.vert`/`lit.frag`): Cook-Torrance (GGX + Smith +
Fresnel-Schlick) con metallic/roughness real, más una aproximación barata de
ambiente tipo IBL (ver Fase 1.5), shadow map del primer light de la escena
con PCF (ver Fase 2) y SSAO de hemisferio sobre el término ambiente (ver Fase
3, `SsaoPass`) — ya no es el Phong sin sombras de la Fase 5 original.
OpenGL (`OpenGLRenderer`) se quedó en Phong clásico a propósito (decisión de
Core, ver "Pendiente de confirmar" más abajo, ya resuelta), sin sombras ni
SSAO. Sigue sin reflejos/GI reales — eso es lo que cubre la fase 4.

## Fases propuestas (orden de menor a mayor riesgo/esfuerzo)

1. ~~**Materiales PBR**~~ **HECHO (2026-10-30).** Cook-Torrance: metallic/roughness
   + Fresnel-Schlick + GGX en vez de Phong, en el backend Vulkan. Cambió
   `Material` (baseColor/metallic/roughness), `LitPushConstants`/`lit.vert`/
   `lit.frag`, el editor de materiales en `MainUi.cpp` (sliders Metálico/
   Rugosidad, 2 sitios + preview de `icons::materialBall`) y el formato
   `.iron` (`SceneSerializer.cpp`: `specular`/`shininess` → `metallic`/
   `roughness`, sin migración — decisión de Core). `OpenGLRenderer` recibió
   solo un shim de compatibilidad (mapea metallic/roughness a specular/
   shininess aproximados) para que su Phong siga compilando y viéndose
   razonable, sin PBR real ahí — Vulkan-only por elección explícita de Core.
   Verificado en vivo: material dieléctrico (0% metálico) se ve correctamente
   sombreado.
1.5. **HECHO (2026-10-30).** Aproximación de IBL sin HDRI/cubemap ("hemisphere
   ambient": color sólido de cielo arriba / suelo abajo, muestreado por normal
   para el difuso y por el vector de reflexión para el especular, con la
   aproximación polinómica de Karis para el BRDF de entorno — ver comentarios
   en `lit.frag`). Verificado en vivo que hacía falta: con el hack plano
   anterior (`ambient = ambientStrength * baseColor`), un material casi-espejo
   (96% metálico, 1% rugoso) se veía prácticamente negro/invisible en el
   viewport fuera del punto exacto de brillo especular — físicamente correcto
   para Cook-Torrance sin nada que reflejar, pero visualmente inútil para
   iterar materiales. Con el hemisferio, el mismo material ya se ve como un
   cubo sólido con sombreado de caras reconocible. Sigue sin ser IBL real
   (eso necesita HDRI + cubemap prefiltrado + descriptor sets, infraestructura
   que no existía y que se evitó a propósito aquí) — ni sombras. Candidato a
   reemplazar más adelante por una Fase de IBL real con HDRI si hace falta
   más fidelidad (reflejos de un entorno real, no un degradado de 2 colores).
2. ~~**Sombras**~~ **HECHO (2026-10-30).** Shadow map del primer light de la
   escena (ortográfico, PCF 3x3 con slope-scaled bias): render pass offscreen
   depth-only (`ShadowMap.cpp`), pipeline vertex-only (`ShadowPipeline.cpp`),
   `lightViewProj` compartida entre `shadow.vert` y `lit.vert`/`lit.frag`
   (`main.cpp`: `computeLightViewProj()`). Bug real encontrado solo al
   verificar en vivo (compilaba limpio y no daba validation errors, pero no
   se veía ninguna sombra): la ortográfica de la luz usaba constantes en
   "cm" literal (`extent=600`, `far=5000`) mientras el resto del motor opera
   en unidades crudas de escena (`kCmPerUnit=200` en `MainUi.cpp` solo
   convierte en los campos de la UI) — el frustum salía ~200x mas grande que
   la escena real, reduciendo cualquier sombra a 1-2 pixeles invisibles del
   mapa de 2048. Corregido escalando `extent`/`near`/`far` a unidades crudas;
   confirmado en vivo con un cubo sobre un plano.
3. ~~**Oclusión ambiental**~~ **HECHO (2026-11-03), Vulkan-only (mismo criterio
   que fases 1/1.5/2).** SSAO de hemisferio con 16 muestras (kernel hardcodeado,
   espiral de ángulo dorado + sesgo cuadrático, sin textura de ruido — rotación
   por píxel vía hash de `gl_FragCoord`), 3 pasadas propias encadenadas dentro
   de `SsaoPass` (`gbuffer.vert/.frag` → `ssao.frag` → `blur.frag`, blur de
   caja 4x4): G-buffer de posición+normal en espacio de vista a resolución de
   framebuffer completo (recreado en `resize()`, no fijo como `ShadowMap`),
   mismo patrón de encapsulación `beginPass/draw/endPass` que `ShadowMap`. La
   oclusión solo atenúa el término ambiente de `lit.frag` (15% de intensidad
   por diseño, ver `kAmbientStrength`), nunca la luz directa — misma
   separación que ya hace la sombra ahí. `LitPipeline` ganó un segundo
   descriptor set (set=1, el mapa de AO difuminado) junto al de sombras.
   Compila limpio (MSVC, los 5 shaders nuevos compilan a SPIR-V sin error) y
   la app corre estable a 600-950 FPS con las 3 pasadas nuevas ejecutándose
   cada frame sin validation errors ni crashes — evidencia fuerte de que la
   sincronización Vulkan (dependencias `VK_SUBPASS_EXTERNAL` por render pass,
   mismo patrón que `ShadowMap`) es correcta. **Verificación visual pixel a
   pixel incompleta**: se intentó confirmar el oscurecimiento de contacto
   cubo-plano en vivo, pero la automatización de UI nativa en Windows (clics
   ciegos por coordenadas sobre ventana ImGui) no permitió editar con
   fiabilidad la posición del cubo para un test limpio de "objeto apoyado en
   plano" sin arriesgar más corrupción del estado de la escena de prueba — se
   abortó esa vía antes de forzar más cambios. Pendiente: una sesión con
   capacidad de interacción nativa fiable (o un escenario de prueba cargado
   desde archivo `.iron`) para confirmar visualmente el oscurecimiento de
   contacto, aunque el efecto es sutil por diseño (solo 15% del ambiente).
4. **Reflejos e iluminación indirecta vía ray tracing por hardware**
   (`VK_KHR_ray_query` o `VK_KHR_ray_tracing_pipeline` — la RTX 3070 Ti del PC
   principal tiene núcleos RT reales). Esto es lo que ya apuntaba `AGENTS.md`
   §2 ("Render Vulkan RT / OptiX en vez de ProRender"). Fase grande: nueva
   extensión de dispositivo, acceleration structures (BLAS/TLAS) que hay que
   reconstruir cuando la escena cambia, shaders de raygen/hit/miss.
5. (Opcional, más adelante) un modo de "render final" separado del viewport en
   tiempo real — acumulación de muestras tipo path tracer offline para la
   vista de Render, igual que C4D distingue viewport (rápido, aproximado) de
   render final (lento, de calidad) — en vez de forzar todo el rigor de un
   path tracer a 144Hz en el viewport.

## Por qué este orden

Fases 1-3 son rasterizador puro: verificables sin hardware RT, bajo riesgo de
romper algo de forma silenciosa, y ya suponen una mejora visual honesta y
real frente al Standard. La Fase 4 es la que de verdad acerca a "mejor que
C4D" en el sentido fuerte (reflejos/GI reales), pero es donde está la mayor
parte del esfuerzo y del riesgo técnico — tiene sentido no empezar por ahí.

## Resuelto con Core (2026-10-30)

- **OpenGL = modo simple/compatibilidad**, Phong intacto con un shim de
  mapeo metallic/roughness → specular/shininess; todo el esfuerzo PBR real
  (y lo que venga después: sombras, SSAO, RT) va solo a Vulkan.
- **Formato `.iron` roto sin migración** es aceptable — el tokenizador ya
  ignora claves desconocidas, así que un `.iron` viejo con `specular`/
  `shininess` carga igual (esos campos se pierden en silencio, el resto no).
