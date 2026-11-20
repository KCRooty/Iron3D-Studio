#pragma once

struct GLFWwindow;

namespace iron3d {

// Solo para verificacion ("--size=WxH"): en Windows deja que la ventana sea mayor que la pantalla
// (por defecto el sistema la recorta a la pantalla) y le da ese tamano de area cliente. Sin Windows no hace nada.
void forceWindowClientSize(GLFWwindow* window, int width, int height);

} // namespace iron3d
