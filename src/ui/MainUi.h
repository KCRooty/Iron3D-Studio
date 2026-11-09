#pragma once

// Interfaz estilo Cinema 4D R19 ("Entorno de Arranque"), ver
// V:\ObsidianVault\14-Iron3D-CoalMix\c4d_ui_reference_visual.md: menu
// principal completo, barra de iconos superior y de modos a la izquierda,
// visor con su propio menu y overlays, Gestor de Objetos + Atributos a la
// derecha, Linea de Tiempo, Gestor de Materiales + Coordenadas abajo y barra
// de ayuda contextual. Los iconos son dibujos propios (ImDrawList), no
// recursos de C4D. Lo que todavia no existe en Iron3D aparece deshabilitado
// en su sitio, nunca fingiendo que funciona.

#include "UiPreset.h"
#include "core/Material.h"
#include "core/OrbitalCamera.h"
#include "core/Preferences.h"
#include "core/SceneHistory.h"
#include "core/SceneNode.h"
#include "core/TransformGizmo.h"

#include <glm/glm.hpp>

#include <filesystem>
#include <vector>

namespace iron3d {

// Lo que la UI decide cada frame y main.cpp necesita para dibujar la escena.
struct ViewportState {
    // Rectangulo del area 3D en coordenadas de ventana (las de ImGui/GLFW).
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    bool showGrid = true;
    // Herramienta "Seleccion en Vivo" activa: sin gizmo, el clic solo selecciona.
    bool selectTool = false;
    // Con selectTool: 0 Seleccion en Vivo (9), 1 Rectangular (0), 2 Lazo (8).
    // En 1 y 2 el clic no elige objeto: arrastrar dibuja la zona (MainUi).
    int selectShape = 0;
    // Archivo > Salir: main.cpp cierra la ventana al ver esto.
    bool requestQuit = false;
    // Render > Renderizar Vista Activa (Ctrl+R): el visor muestra el "render"
    // (fondo negro, sin rejilla, gizmo ni contorno) hasta la siguiente
    // interaccion con el visor, como en C4D.
    bool renderView = false;

    // F5 "Todas las Vistas": cuatro paneles (Perspectiva, Superior, Derecha,
    // Frontal), cada uno con su propia camara. La activa es la `camera` de
    // siempre; las otras tres esperan en quadCams. Con quad, x/y/w/h son los
    // del panel activo (picking, gizmo y seleccion por zona van ahi) y
    // fullX/fullY/fullW/fullH los del visor entero.
    bool quad = false;
    int quadActive = 0;
    // W: ejes del gizmo en el mundo (si no, los del objeto, como C4D por defecto).
    bool worldCoords = false;
    // L "Activar Eje": mover/rotar/escalar cambia solo el eje del objeto; la
    // geometria (si es un objeto editable) y los hijos se quedan donde estan.
    bool axisMode = false;
    // Modo de edicion: 0 Modelo, 1 Puntos, 2 Aristas, 3 Poligonos (estos tres
    // solo hacen algo con un objeto editable -- Objeto Poligono --).
    int compMode = 0;
    OrbitalCamera quadCams[4];
    float fullX = 0.0f, fullY = 0.0f, fullW = 0.0f, fullH = 0.0f;
};

// Modo Eje (L): tras mover el eje de `node` (su matriz de mundo era
// `beforeWorld`), deja sus hijos y, si es editable, su malla donde estaban.
void compensateAxisMove(SceneNode& node, const glm::mat4& beforeWorld);

// Libera las texturas propias de la UI (Visor de Imagenes) antes de apagar el renderer.
void releaseUiTextures();

// main.cpp avisa al soltar el gizmo: con Fotogramas Clave Automaticos
// (Ctrl+F9) se graba una clave de los objetos activos.
void notifyTransformEdited(SceneNode& root);

// --- Modos Puntos / Aristas / Poligonos (main.cpp) -----------------------------
// Con componentes seleccionados en el objeto activo, el gizmo agarra un nodo
// auxiliar en su centro (devuelto aqui; nullptr si no aplica). `dragging`:
// mientras se arrastra no se recoloca.
SceneNode* componentGizmoTarget(SceneNode* selected, bool dragging);
void beginComponentDrag(SceneNode& node);
// Tras cada paso de arrastre: aplica a los puntos lo que se ha movido el nodo auxiliar.
void applyComponentDrag(SceneNode& node);
// Clic en el visor (coordenadas de ventana). true si lo gestiona el modo de
// componentes (y entonces no se elige objeto).
bool componentClick(SceneNode* selected, const OrbitalCamera& camera, float mouseX, float mouseY, bool shift, bool ctrl);
// Solo el hit-test del clic: indice del componente bajo el raton, -1 si no hay
// ninguno y -2 si no se esta en un modo de componentes (no cambia la seleccion).
int componentHit(SceneNode* selected, const OrbitalCamera& camera, float mouseX, float mouseY);
bool componentSelected(int index);
void clearComponentSelection();

// --- Herramientas de modelado (Extruir D, Extrusion Interna I, Biselar M~S, Cuchillo K)
// Con una herramienta activa no hay gizmo: arrastrar en el visor la aplica
// (en horizontal cambia la cantidad; el Cuchillo dibuja la linea de corte).
bool modelToolActive();
// Raton en el visor (coordenadas de ventana). Se llama cada fotograma con
// la herramienta activa; tambien con leftDown=false fuera del visor (suelta).
void modelToolMouse(SceneNode& root, SceneNode* selected, const OrbitalCamera& camera, float mouseX, float mouseY,
                    bool leftDown, bool leftPressed, SceneHistory& history);

// Rectangulo (coordenadas de ventana) del panel i (0-3) de la vista de 4.
struct QuadRect { float x, y, w, h; };
QuadRect quadPanelRect(const ViewportState& vp, int i);

// Muestra la ventana con el resultado de importar un .c4d (main.cpp la usa al
// abrir un .c4d pasado por linea de comandos).
struct C4DImportReport;
void showImportReport(const C4DImportReport& report);

// Encuadra la camara sobre todo lo que hay en la escena (como "Encuadrar
// todo" de C4D). main.cpp lo usa tras abrir una escena por linea de comandos.
void frameScene(OrbitalCamera& camera, const SceneNode& root);
// Encuadra un objeto concreto (con sus hijos), como la tecla O.
void frameNode(OrbitalCamera& camera, const SceneNode& node);
// Lo mismo que pulsar la flecha de objeto fuera de vista: la camara viaja
// hasta encuadrar el objeto (animado, en los siguientes fotogramas).
void travelToNode(OrbitalCamera& camera, const SceneNode& node);

// `selected` cambia al hacer clic en otro objeto, duplicarlo, borrarlo o al
// cargar/deshacer/rehacer una escena entera encima. `gizmoMode` se cambia
// desde la barra de iconos o con E/R/T (atajos de C4D). `history` es la pila
// de deshacer/rehacer; `materials` la biblioteca global de la escena.
void drawMainUi(SceneNode& root, SceneNode*& selected, GizmoMode& gizmoMode,
                 const std::filesystem::path& scenesDir, SceneHistory& history, float framerate,
                 RendererBackend activeBackend, Preferences& prefs, const std::filesystem::path& prefsPath,
                 std::vector<Material>& materials, OrbitalCamera& camera, ViewportState& viewport);

// --- Seleccion multiple -------------------------------------------------------
// El conjunto de objetos seleccionados vive en MainUi.cpp; `selected` es el
// objeto ACTIVO (el que muestran Atributos y Coordenadas y el que agarra el
// gizmo). En el visor se anade/quita con Mayus+clic y en el Gestor de Objetos
// con Ctrl+clic, igual que en C4D.

// Mayus+clic en el visor sobre `hit`: lo anade o lo quita. Devuelve el nuevo
// objeto activo (main.cpp se lo asigna a `selected`).
SceneNode* toggleViewportSelection(SceneNode& root, SceneNode* hit, SceneNode* active);

// Cuantos objetos hay seleccionados ahora mismo.
int selectionCount();

// true si `node` forma parte de la seleccion del visor.
bool viewportSelectionContains(const SceneNode* node);
// La seleccion pasa a ser solo `node` (main.cpp asigna luego `selected`).
void viewportSelectOnly(SceneNode* node);

// Centro de la seleccion en mundo: ahi va el gizmo cuando hay varios objetos.
glm::vec3 selectionPivot(SceneNode& root, SceneNode* active);

// Reparte al resto de la seleccion el paso de arrastre que el gizmo acaba de
// aplicar al objeto activo (desplazamiento, rotacion y escala), orbitando
// todo alrededor de `pivot`.
void applySelectionDrag(SceneNode& root, SceneNode* active, const glm::vec3& pivot,
                        const glm::vec3& worldDelta, const glm::vec3& rotDeltaDeg,
                        const glm::vec3& scaleRatio);

// Preset de interfaz (UiPreset.h): paleta de color, metricas de estilo y
// disposicion/nombres de los paneles. Llamar una vez tras
// ImGui::CreateContext() y antes de iniciar el backend de render (carga la
// fuente del sistema en el atlas la primera vez) y de nuevo cada vez que Core
// cambia algo en Preferencias > Interfaz. El preset por defecto reproduce el
// C4D de siempre.
void applyPreset(const UiPreset& preset);

// Hoja de verificacion con todos los iconos y su nombre ("--icon-sheet").
// Deja el visor 3D en un rectangulo minimo para que no tape la hoja.
void drawIconSheet(ViewportState& viewport);

// Abre Preferencias al arrancar ("--prefs", para verificar la pagina
// Interfaz sin tener que pulsar nada).
void openPreferencesDialog();

// Muestra el Navegador de Contenido en el hueco del Gestor de Objetos (main.cpp
// lo usa al abrir Iron3D con un .lib4d).
void openContentBrowser();

// "--ui-cmd=<orden>" (verificacion por captura sin clics): make-editable,
// frame-selected, commander[:texto] o vp-menu (menu del clic derecho en el
// centro del visor). Devuelve false si la orden no existe.
bool queueUiCommand(const std::string& cmd);

// Carga un modelo de una libreria .lib4d en la escena, como un doble clic en
// el navegador (se puede deshacer). main.cpp lo usa con "--load-preset=".
class PresetLibrary;
struct PresetItem;
void loadPresetIntoScene(const PresetLibrary& lib, const PresetItem& item, SceneNode& root, SceneNode*& selected,
                         SceneHistory& history, std::vector<Material>& materials);

} // namespace iron3d
