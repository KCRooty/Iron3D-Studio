#include "MainUi.h"
#include "core/SceneSerializer.h"

#include <imgui.h>

#include <algorithm>
#include <vector>
#include <cstring>

namespace iron3d {

namespace {

// Visibilidad de las "ventanas de dialogo" (Escena, Preferencias): en C4D de
// verdad no son paneles acoplados sino dialogos que abres desde un menu
// (Archivo > Guardar, Edicion > Preferencias) -- aqui igual, controlado por
// drawMenuBar() y leido por el resto de drawMainUi() en el mismo frame.
bool g_showScenePanel = false;
bool g_showPreferencesPanel = false;

// Nombre por defecto al crear cada tipo de primitiva desde el menu contextual
// del Gestor de Objetos o el panel Crear -- igual de informativo que el
// nombre de clase de C4D al arrastrar un primitivo nuevo (p.ej. "Cube").
const char* defaultNameFor(PrimitiveType type) {
    switch (type) {
        case PrimitiveType::Cube: return "Cubo";
        case PrimitiveType::Sphere: return "Esfera";
        case PrimitiveType::Plane: return "Plano";
        case PrimitiveType::Light: return "Luz";
        case PrimitiveType::None: default: return "Nulo";
    }
}

void drawNodeTree(SceneNode& node, SceneNode*& selected, SceneNode*& toDelete,
                   SceneNode& sceneRoot, SceneHistory& history) {
    ImGui::PushID(node.id());

    bool isLeaf = node.children().empty();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                ImGuiTreeNodeFlags_SpanAvailWidth;
    if (&node == selected) flags |= ImGuiTreeNodeFlags_Selected;
    if (isLeaf) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    ImGui::Checkbox("##vis", &node.visible);
    ImGui::SameLine();
    bool opened = ImGui::TreeNodeEx(node.name().c_str(), flags);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        selected = &node;
    }

    SceneNode* pendingAddTo = nullptr;
    PrimitiveType pendingAddType = PrimitiveType::None;
    SceneNode* pendingDuplicate = nullptr;
    if (ImGui::BeginPopupContextItem("ctx")) {
        if (ImGui::BeginMenu("Anadir")) {
            if (ImGui::MenuItem("Nulo (vacio)")) { pendingAddTo = &node; pendingAddType = PrimitiveType::None; }
            if (ImGui::MenuItem("Cubo")) { pendingAddTo = &node; pendingAddType = PrimitiveType::Cube; }
            if (ImGui::MenuItem("Esfera")) { pendingAddTo = &node; pendingAddType = PrimitiveType::Sphere; }
            if (ImGui::MenuItem("Plano")) { pendingAddTo = &node; pendingAddType = PrimitiveType::Plane; }
            if (ImGui::MenuItem("Luz")) { pendingAddTo = &node; pendingAddType = PrimitiveType::Light; }
            ImGui::EndMenu();
        }
        bool canDelete = node.parent() != nullptr; // la raiz de la escena no se borra ni se duplica
        if (ImGui::MenuItem("Duplicar", "Ctrl+D", false, canDelete)) pendingDuplicate = &node;
        if (ImGui::MenuItem("Eliminar", nullptr, false, canDelete)) toDelete = &node;
        ImGui::EndPopup();
    }

    if (opened && !isLeaf) {
        // Copia de los punteros hijos ANTES de recursar: anadir/eliminar se aplica
        // despues de este bucle, nunca durante, para no invalidar el vector en marcha.
        std::vector<SceneNode*> childPtrs;
        childPtrs.reserve(node.children().size());
        for (const auto& c : node.children()) childPtrs.push_back(c.get());
        for (SceneNode* c : childPtrs) drawNodeTree(*c, selected, toDelete, sceneRoot, history);
        ImGui::TreePop();
    }

    if (pendingAddTo) {
        history.commitBefore(sceneRoot);
        pendingAddTo->addChild(defaultNameFor(pendingAddType), pendingAddType);
    }
    if (pendingDuplicate && pendingDuplicate->parent()) {
        history.commitBefore(sceneRoot);
        selected = duplicateSubtree(*pendingDuplicate->parent(), *pendingDuplicate);
    }
    ImGui::PopID();
}

// Pestana con estilo de tab pero sin contenido propio, para los huecos del
// layout estilo C4D que todavia no tienen funcionalidad real detras
// (Etiquetas, Editar/Funcion/Textura) -- se ve en su sitio sin fingir que
// hace algo. BeginDisabled la atenua y le quita el clic.
void stubTab(const char* label) {
    ImGui::BeginDisabled();
    ImGui::TabItemButton(label);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Todavia no implementado");
}

void drawAttributesPanel(SceneNode* selected, SceneNode& sceneRoot, SceneHistory& history) {
    ImGui::Begin("Atributos", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    if (ImGui::BeginTabBar("##attrTabs")) {
        if (ImGui::BeginTabItem("Objeto")) {
            if (!selected) {
                ImGui::TextDisabled("Ningun objeto seleccionado.");
            } else {
                // Patron activar/confirmar de ImGui para deshacer: un campo se puede
                // editar durante varios frames seguidos (arrastrar, escribir), asi
                // que el "antes" se toma solo al primer frame en que el campo se
                // activa -- `preWidgetSnap` es el estado justo antes de que CUALQUIER
                // campo de este panel toque nada este frame, capturado antes de
                // dibujar el primero -- y se empuja a la pila una sola vez, al
                // soltar el campo. Nunca uno por frame/tecla, o la pila se llenaria
                // de ruido.
                const std::string preWidgetSnap = history.snapshot(sceneRoot);
                static std::string pendingSnap;
                auto trackEdit = [&]() {
                    if (ImGui::IsItemActivated()) pendingSnap = preWidgetSnap;
                    if (ImGui::IsItemDeactivatedAfterEdit()) history.commit(pendingSnap);
                };

                char nameBuf[128];
                std::snprintf(nameBuf, sizeof(nameBuf), "%s", selected->name().c_str());
                if (ImGui::InputText("Nombre", nameBuf, sizeof(nameBuf))) {
                    selected->setName(nameBuf);
                }
                trackEdit();
                ImGui::TextDisabled("Tipo: %s", primitiveTypeName(selected->primitive));
                ImGui::Separator();
                ImGui::DragFloat3("Posicion", &selected->position.x, 0.05f);
                trackEdit();
                ImGui::DragFloat3("Rotacion", &selected->rotationDeg.x, 1.0f);
                trackEdit();
                ImGui::DragFloat3("Escala", &selected->scale.x, 0.05f, 0.01f, 100.0f);
                trackEdit();
                ImGui::Separator();
                ImGui::Checkbox("Visible", &selected->visible);
                trackEdit();
                ImGui::TextDisabled("%zu hijo(s)", selected->children().size());
            }
            ImGui::EndTabItem();
        }
        stubTab("Etiquetas");
        ImGui::EndTabBar();
    }
    ImGui::End();
}

// Guardar/cargar escena a disco (Fase 2c, formato .iron -- ver
// SceneSerializer.h). Ahora es un dialogo que se abre desde Archivo (Fase
// "layout C4D"), no un panel siempre acoplado -- `open` es el bool del menu,
// ImGui lo pone a false solo si se cierra con la X.
void drawScenePanel(SceneNode& root, SceneNode*& selected, const std::filesystem::path& scenesDir,
                     SceneHistory& history, bool* open) {
    if (!*open) return;
    static char filenameBuf[128] = "escena";
    static std::string statusMsg;
    static bool statusIsError = false;

    ImGui::Begin("Escena", open);
    ImGui::InputText("Archivo", filenameBuf, sizeof(filenameBuf));
    ImGui::TextDisabled(".iron en %s", scenesDir.string().c_str());

    const std::filesystem::path path = scenesDir / (std::string(filenameBuf) + ".iron");

    if (ImGui::Button("Guardar")) {
        std::string err;
        if (saveScene(path, root, &err)) {
            statusMsg = "Guardado: " + path.filename().string();
            statusIsError = false;
        } else {
            statusMsg = "Error al guardar: " + err;
            statusIsError = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cargar")) {
        std::string err;
        if (loadScene(path, root, &err)) {
            selected = nullptr; // la escena entera cambio, cualquier puntero viejo es invalido
            history.reset(); // un deshacer no debe saltar a una escena que ya no existe
            statusMsg = "Cargado: " + path.filename().string();
            statusIsError = false;
        } else {
            statusMsg = "Error al cargar: " + err;
            statusIsError = true;
        }
    }
    if (!statusMsg.empty()) {
        ImGui::TextColored(statusIsError ? ImVec4(0.95f, 0.35f, 0.35f, 1.0f) : ImVec4(0.4f, 0.9f, 0.4f, 1.0f),
                            "%s", statusMsg.c_str());
    }
    ImGui::End();
}

// Eleccion de backend de render (Fase 3c): solo guarda en disco, nunca
// cambia el renderer en caliente. Dialogo (Edicion > Preferencias), igual
// que Escena.
void drawPreferencesPanel(RendererBackend activeBackend, Preferences& prefs,
                           const std::filesystem::path& prefsPath, bool* open) {
    if (!*open) return;
    static std::string statusMsg;

    ImGui::Begin("Preferencias", open);
    ImGui::Text("Backend activo esta sesion: %s", activeBackend == RendererBackend::Vulkan ? "Vulkan" : "OpenGL");
    ImGui::Separator();
    ImGui::TextUnformatted("Backend de render (se aplica al reiniciar):");
    int backendIdx = (prefs.renderBackend == RendererBackend::Vulkan) ? 0 : 1;
    if (ImGui::RadioButton("Vulkan", backendIdx == 0)) prefs.renderBackend = RendererBackend::Vulkan;
    ImGui::SameLine();
    if (ImGui::RadioButton("OpenGL", backendIdx == 1)) prefs.renderBackend = RendererBackend::OpenGL;

    if (ImGui::Button("Guardar")) {
        statusMsg = savePreferences(prefsPath, prefs) ? "Guardado. Reinicia Iron3D Studio para aplicarlo."
                                                        : "Error al guardar las preferencias.";
    }
    if (!statusMsg.empty()) ImGui::TextUnformatted(statusMsg.c_str());
    ImGui::End();
}

// Atajos de teclado globales (Fase 2d/2e): no dependen de ninguna ventana
// concreta, se comprueban una vez por frame antes de dibujar nada. Un
// undo/redo reconstruye el arbol entero (ver SceneHistory), asi que
// cualquier seleccion vieja queda invalida y se limpia aqui mismo.
void handleGlobalShortcuts(GizmoMode& mode, SceneNode& root, SceneNode*& selected, SceneHistory& history) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard) return;

    if (ImGui::IsKeyPressed(ImGuiKey_1)) mode = GizmoMode::Translate;
    if (ImGui::IsKeyPressed(ImGuiKey_2)) mode = GizmoMode::Rotate;
    if (ImGui::IsKeyPressed(ImGuiKey_3)) mode = GizmoMode::Scale;

    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)) {
        if (history.undo(root)) selected = nullptr;
    }
    if (io.KeyCtrl && ((io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)) || ImGui::IsKeyPressed(ImGuiKey_Y))) {
        if (history.redo(root)) selected = nullptr;
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D) && selected && selected->parent()) {
        history.commitBefore(root);
        selected = duplicateSubtree(*selected->parent(), *selected);
    }
}

// Menu superior (Archivo/Edicion), como cualquier DCC de verdad: Guardar/
// Cargar escena y Preferencias dejan de ser paneles siempre visibles y pasan
// a ser dialogos que abres desde aqui -- Deshacer/Rehacer tambien viven en
// Edicion ademas de en la barra de iconos, igual que en C4D.
void drawMenuBar(SceneNode& root, SceneNode*& selected, SceneHistory& history) {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("Archivo")) {
        if (ImGui::MenuItem("Guardar / cargar escena...")) g_showScenePanel = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edicion")) {
        if (ImGui::MenuItem("Deshacer", "Ctrl+Z", false, history.canUndo())) {
            if (history.undo(root)) selected = nullptr;
        }
        if (ImGui::MenuItem("Rehacer", "Ctrl+Y", false, history.canRedo())) {
            if (history.redo(root)) selected = nullptr;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Preferencias...")) g_showPreferencesPanel = true;
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

// Barra de iconos (estilo C4D: fila fija bajo el menu, todo el ancho). Sin
// fuente de iconos propia todavia -- se usan etiquetas cortas + tooltip con
// el nombre completo en vez de anadir una dependencia nueva solo por esto.
void drawIconToolbar(GizmoMode& mode, SceneNode& root, SceneNode*& selected, SceneHistory& history,
                      RendererBackend activeBackend, const ImVec2& pos, const ImVec2& size) {
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::Begin("##toolbar", nullptr,
                  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);

    auto modeButton = [&](const char* label, const char* tip, GizmoMode m) {
        bool active = (mode == m);
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(label)) mode = m;
        if (active) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    };
    modeButton("Mov", "Mover (1)", GizmoMode::Translate);
    ImGui::SameLine();
    modeButton("Rot", "Rotar (2)", GizmoMode::Rotate);
    ImGui::SameLine();
    modeButton("Esc", "Escalar (3)", GizmoMode::Scale);

    ImGui::SameLine();
    ImGui::Separator();
    ImGui::SameLine();

    if (!history.canUndo()) ImGui::BeginDisabled();
    if (ImGui::Button("Deshacer")) {
        if (history.undo(root)) selected = nullptr;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Deshacer (Ctrl+Z)");
    if (!history.canUndo()) ImGui::EndDisabled();
    ImGui::SameLine();
    if (!history.canRedo()) ImGui::BeginDisabled();
    if (ImGui::Button("Rehacer")) {
        if (history.redo(root)) selected = nullptr;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rehacer (Ctrl+Y)");
    if (!history.canRedo()) ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::Separator();
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("Navegacion (estilo C4D):");
        ImGui::BulletText("Alt + clic izq: orbitar");
        ImGui::BulletText("Alt + clic medio: panear");
        ImGui::BulletText("Alt + clic der / rueda: zoom");
        ImGui::BulletText("Clic en un eje del gizmo: mover/rotar/escalar");
        ImGui::BulletText("Clic derecho en un nodo: anadir primitiva / duplicar / eliminar");
        ImGui::BulletText("Ctrl+Z / Ctrl+Y: deshacer / rehacer -- Ctrl+D: duplicar seleccionado");
        ImGui::EndTooltip();
    }

    // Lectura rapida del backend activo -- a la derecha, igual de util que
    // el indicador de API de render en la barra de C4D.
    const char* backendLabel = activeBackend == RendererBackend::Vulkan ? "Vulkan" : "OpenGL";
    const float labelWidth = ImGui::CalcTextSize(backendLabel).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - labelWidth - ImGui::GetStyle().WindowPadding.x);
    ImGui::TextDisabled("%s", backendLabel);

    ImGui::End();
}

// Panel "Crear" (esquina inferior izquierda, igual que las pestanas Crear/
// Editar/Funcion/Textura de C4D): aqui solo Crear tiene contenido real --
// anade la primitiva como hijo del nodo seleccionado, o de la raiz si no hay
// seleccion. Los otros tres son huecos del layout sin funcionalidad todavia.
void drawCreatePanel(SceneNode& root, SceneNode*& selected, SceneHistory& history,
                      const ImVec2& pos, const ImVec2& size) {
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::Begin("Crear", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    if (ImGui::BeginTabBar("##createTabs")) {
        if (ImGui::BeginTabItem("Crear")) {
            SceneNode* target = selected ? selected : &root;
            auto createButton = [&](const char* label, PrimitiveType type) {
                if (ImGui::Button(label, ImVec2(-1, 0))) {
                    history.commitBefore(root);
                    selected = target->addChild(defaultNameFor(type), type);
                }
            };
            createButton("Cubo", PrimitiveType::Cube);
            createButton("Esfera", PrimitiveType::Sphere);
            createButton("Plano", PrimitiveType::Plane);
            createButton("Luz", PrimitiveType::Light);
            createButton("Nulo", PrimitiveType::None);
            ImGui::EndTabItem();
        }
        stubTab("Editar");
        stubTab("Funcion");
        stubTab("Textura");
        ImGui::EndTabBar();
    }
    ImGui::End();
}

// Franja de estado inferior (FPS + "linea de tiempo"): Iron3D no tiene
// animacion todavia, asi que los controles de reproduccion se muestran
// deshabilitados en vez de fingir que funcionan -- hueco del layout fiel a
// C4D, honesto sobre lo que hace de verdad.
void drawStatusBar(float framerate, const ImVec2& pos, const ImVec2& size) {
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::Begin("##status", nullptr,
                  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);

    ImGui::Text("%.1f FPS (%.2f ms/frame)", framerate, 1000.0f / framerate);
    ImGui::SameLine();
    ImGui::Separator();
    ImGui::SameLine();

    ImGui::BeginDisabled();
    ImGui::Button("|<");
    ImGui::SameLine();
    ImGui::Button(">");
    ImGui::SameLine();
    ImGui::Button(">|");
    ImGui::SameLine();
    ImGui::TextUnformatted("Fotograma 0/0");
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Animacion: todavia no implementada");

    ImGui::End();
}

} // namespace

void drawMainUi(SceneNode& root, SceneNode*& selected, GizmoMode& gizmoMode,
                 const std::filesystem::path& scenesDir, SceneHistory& history, float framerate,
                 RendererBackend activeBackend, Preferences& prefs, const std::filesystem::path& prefsPath) {
    SceneNode* toDelete = nullptr;

    handleGlobalShortcuts(gizmoMode, root, selected, history);
    drawMenuBar(root, selected, history);

    // Layout fijo estilo C4D: toda la medida sale del viewport principal
    // cada frame, asi que se adapta solo al redimensionar la ventana (igual
    // que el resto de la app, ver resizeIfNeeded en main.cpp).
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float menuBarH = ImGui::GetFrameHeight();
    const float toolbarH = 34.0f;
    const float statusH = 30.0f;
    const float rightW = std::min(std::max(vp->Size.x * 0.24f, 300.0f), 380.0f);
    // 300px minimo: con menos, las 4 pestanas (Crear/Editar/Funcion/Textura)
    // se amontonan y se solapan visualmente -- visto al comprobarlo en vivo.
    const float createW = std::min(std::max(vp->Size.x * 0.22f, 300.0f), 380.0f);
    // 210px: pestana + 5 botones de primitiva (Cubo/Esfera/Plano/Luz/Nulo)
    // completos sin tener que hacer scroll -- con 150 el ultimo quedaba
    // cortado, visto al comprobarlo en vivo.
    const float createH = 210.0f;

    const float contentTop = vp->Pos.y + menuBarH + toolbarH;
    const float contentBottom = vp->Pos.y + vp->Size.y - statusH;
    const float contentH = contentBottom - contentTop;
    const float objectsH = contentH * 0.45f;

    drawIconToolbar(gizmoMode, root, selected, history, activeBackend,
                     ImVec2(vp->Pos.x, vp->Pos.y + menuBarH), ImVec2(vp->Size.x, toolbarH));
    drawStatusBar(framerate, ImVec2(vp->Pos.x, vp->Pos.y + vp->Size.y - statusH), ImVec2(vp->Size.x, statusH));
    drawCreatePanel(root, selected, history, ImVec2(vp->Pos.x, contentBottom - createH), ImVec2(createW, createH));

    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - rightW, contentTop), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(rightW, objectsH), ImGuiCond_Always);
    ImGui::Begin("Gestor de Objetos", nullptr,
                  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    drawNodeTree(root, selected, toDelete, root, history);
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - rightW, contentTop + objectsH), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(rightW, contentH - objectsH), ImGuiCond_Always);
    drawAttributesPanel(selected, root, history);

    drawScenePanel(root, selected, scenesDir, history, &g_showScenePanel);
    drawPreferencesPanel(activeBackend, prefs, prefsPath, &g_showPreferencesPanel);

    if (toDelete && toDelete->parent()) {
        history.commitBefore(root);
        toDelete->parent()->removeChild(toDelete); // tras esta linea `toDelete` ya no existe
        selected = nullptr; // evita un puntero colgante si se borro el seleccionado o un ancestro suyo
    }
}

} // namespace iron3d
