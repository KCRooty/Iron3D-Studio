#pragma once

// Iconos de la interfaz dibujados a mano con ImDrawList: siguen la metafora y
// la familia de color de cada grupo de herramientas de C4D R19 (primitivas
// azules, generadores verdes, deformadores violeta, luces claras...), pero
// son dibujos propios, no recursos extraidos de Cinema 4D.
//
// La lista es una X-macro para poder recorrerla entera (hoja de iconos de
// depuracion, `--icon-sheet`) y tener el nombre de cada uno.

#include <imgui.h>

namespace iron3d::icons {

#define IRON3D_ICON_LIST(X)                                                                                     \
    /* Barra superior */                                                                                         \
    X(Undo) X(Redo) X(LiveSelect) X(Move) X(Scale) X(Rotate) X(AxisX) X(AxisY) X(AxisZ) X(CoordSystem)          \
    X(RenderView) X(RenderPicture) X(RenderSettings) X(Cube) X(Spline) X(Nurbs) X(Cloner) X(Deformer)            \
    X(Floor) X(Camera) X(Light)                                                                                  \
    /* Barra de modos (izquierda) */                                                                             \
    X(MakeEditable) X(ModelMode) X(TextureMode) X(AxisMode) X(Points) X(Edges) X(Polygons) X(Tweak)             \
    X(Solo) X(Snap) X(Workplane) X(Locked)                                                                       \
    /* Objetos (menus y Gestor de Objetos) */                                                                    \
    X(Null) X(Sphere) X(Plane) X(Cone) X(Cylinder) X(Disc) X(Torus) X(Capsule) X(Tube) X(Pyramid) X(Platonic)    \
    X(Figure) X(Landscape) X(Relief) X(Polygon) X(OilTank) X(EmptyPolygon) X(Guide)                              \
    /* Navegacion del visor */                                                                                   \
    X(NavPan) X(NavZoom) X(NavRotate) X(NavLayout)                                                               \
    /* Etiquetas y Linea de Tiempo */                                                                            \
    X(PhongTag) X(Play) X(Pause) X(PlayBack) X(StepFwd) X(StepBack) X(ToStart) X(ToEnd) X(NextKey) X(PrevKey)    \
    X(Record) X(AutoKey) X(KeySel) X(KeyPos) X(KeyScale) X(KeyRot) X(KeyParam) X(KeyPLA)                         \
    /* Varios */                                                                                                 \
    X(Check) X(Lock) X(Search) X(Back) X(Forward) X(Up) X(NewPanel) X(Gear) X(Filter)                            \
    /* Splines (IconsSplines.cpp) */                                                                             \
    X(SplPen) X(SplSketch) X(SplSmooth) X(SplArcTool) X(SplArc) X(SplCircle) X(SplHelix) X(SplNSide)             \
    X(SplRectangle) X(SplStar) X(SplText) X(SplVectorizer) X(SplFourSide) X(SplCissoid) X(SplCog) X(SplCycloid) \
    X(SplFormula) X(SplFlower) X(SplProfile) X(SplSubtract) X(SplUnion) X(SplAnd) X(SplOr) X(SplIntersect)      \
    /* Generadores, Modelado y MoGraph (IconsGenerators.cpp) */                                                  \
    X(GenSubdiv) X(GenExtrude) X(GenLathe) X(GenLoft) X(GenSweep) X(GenBezier)                                   \
    X(ModArray) X(ModAtomArray) X(ModBoole) X(ModSplineMask) X(ModConnect) X(ModSymmetry) X(ModInstance)         \
    X(ModMetaball) X(ModPython) X(ModLod) X(ModPolyReduction)                                                    \
    X(MoCloner) X(MoMatrix) X(MoFracture) X(MoVoronoi) X(MoInstance) X(MoText) X(MoTracer) X(MoSpline)           \
    X(MoExtrude) X(MoPolyFx)                                                                                     \
    /* Deformadores (IconsDeformers.cpp) */                                                                      \
    X(DefBend) X(DefBulge) X(DefShear) X(DefTaper) X(DefTwist) X(DefFfd) X(DefMesh) X(DefSquash) X(DefMelt)      \
    X(DefExplosion) X(DefExplosionFx) X(DefShatter) X(DefCorrection) X(DefJiggle) X(DefMorph) X(DefShrinkWrap)   \
    X(DefSpherify) X(DefSurface) X(DefWrap) X(DefSpline) X(DefSplineRail) X(DefSplineWrap) X(DefCamera)          \
    X(DefCollision) X(DefDisplacer) X(DefFormula) X(DefWind) X(DefSmoothing) X(DefBevel)                         \
    /* Escena, camaras y luces (IconsScene.cpp) */                                                               \
    X(SceSky) X(SceEnvironment) X(SceForeground) X(SceBackground) X(SceStage) X(ScePhysicalSky) X(SceCloudTool)  \
    X(SceCloudGroup) X(SceCloud) X(SceCloudJoin) X(SceGrass)                                                     \
    X(CamTarget) X(CamStereo) X(CamMotion) X(CamMorph) X(CamCrane)                                               \
    X(LgtTarget) X(LgtIes) X(LgtSun) X(LgtSpot) X(LgtArea) X(LgtInfinite) X(LgtPbr) X(LgtTool)                   \
    /* Menus (IconsMenus.cpp) */                                                                                 \
    X(MnuRectSelect) X(MnuLassoSelect) X(MnuPolySelect) X(MnuNew) X(MnuOpen) X(MnuSave) X(MnuSaveAs)             \
    X(MnuExport) X(MnuExit) X(MnuCut) X(MnuCopy) X(MnuPaste) X(MnuDelete) X(MnuSelectAll) X(MnuDeselect)         \
    X(MnuProject) X(MnuPrefs) X(MnuRenderRegion) X(MnuPictureViewer) X(MnuRenderQueue) X(MnuInteractiveRegion)   \
    X(MnuMakePreview) X(MnuNewMaterial) X(MnuNewPbrMaterial) X(MnuLoadMaterial) X(MnuObjManager)                 \
    X(MnuMatManager) X(MnuTimeline) X(MnuAttributes) X(MnuCoords) X(MnuLayers) X(MnuContentBrowser)              \
    X(MnuMatEditor) X(MnuHelp) X(MnuAbout) X(MnuLoop)                                                                \
    /* Navegador de Contenido (IconsMenus.cpp) */                                                             \
    X(CbFolder) X(CbLibrary) X(CbCatalog) X(CbRecent) X(CbFavorites) X(CbComputer) X(CbPresets)             \
    X(CbNetwork) X(CbSearchResults) X(CbModel) X(CbFile)

enum class Icon {
#define IRON3D_ICON_ENUM(name) name,
    IRON3D_ICON_LIST(IRON3D_ICON_ENUM)
#undef IRON3D_ICON_ENUM
    Count_
};

// Nombre del enumerador (hoja de iconos de depuracion).
const char* name(Icon icon);

// Dibuja `icon` dentro del cuadrado [min, min+size].
void draw(ImDrawList* dl, Icon icon, ImVec2 min, float size, bool enabled = true);

// Bola de material sombreada (Gestor de Materiales, Editor de Material,
// etiqueta de textura): degradado radial + brillo especular.
void materialBall(ImDrawList* dl, ImVec2 center, float radius, const ImVec4& color, float specular,
                  float shininess);

// Texto girado 90 grados en sentido horario (pestanas verticales del borde
// derecho). `topRight` es la esquina sup-derecha de la caja de texto girada.
void textVertical(ImDrawList* dl, ImVec2 topRight, ImU32 col, const char* text);
float textVerticalLength(const char* text);

} // namespace iron3d::icons
