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
    // Archivo > Salir: main.cpp cierra la ventana al ver esto.
    bool requestQuit = false;
    // Render > Renderizar Vista Activa (Ctrl+R): el visor muestra el "render"
    // (fondo negro, sin rejilla, gizmo ni contorno) hasta la siguiente
    // interaccion con el visor, como en C4D.
    bool renderView = false;
};

// Muestra la ventana con el resultado de importar un .c4d (main.cpp la usa al
// abrir un .c4d pasado por linea de comandos).
struct C4DImportReport;
void showImportReport(const C4DImportReport& report);

// Encuadra la camara sobre todo lo que hay en la escena (como "Encuadrar
// todo" de C4D). main.cpp lo usa tras abrir una escena por linea de comandos.
void frameScene(OrbitalCamera& camera, const SceneNode& root);

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

} // namespace iron3d
