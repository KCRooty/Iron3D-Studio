#include "WindowSize.h"

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace iron3d {
namespace {
WNDPROC g_original = nullptr;
int g_w = 0, g_h = 0;

LRESULT CALLBACK oversizeProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_GETMINMAXINFO) {
        MINMAXINFO* mm = reinterpret_cast<MINMAXINFO*>(lp);
        mm->ptMaxTrackSize.x = mm->ptMaxSize.x = g_w + 64;
        mm->ptMaxTrackSize.y = mm->ptMaxSize.y = g_h + 64;
        return 0;
    }
    return CallWindowProcW(g_original, h, msg, wp, lp);
}
} // namespace

void forceWindowClientSize(GLFWwindow* window, int width, int height) {
    HWND h = glfwGetWin32Window(window);
    if (!h) return;
    g_w = width;
    g_h = height;
    g_original = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(oversizeProc)));
    RECT r{0, 0, width, height};
    AdjustWindowRectEx(&r, static_cast<DWORD>(GetWindowLongPtrW(h, GWL_STYLE)), FALSE,
                       static_cast<DWORD>(GetWindowLongPtrW(h, GWL_EXSTYLE)));
    SetWindowPos(h, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
}

} // namespace iron3d
#else
namespace iron3d {
void forceWindowClientSize(GLFWwindow*, int, int) {}
} // namespace iron3d
#endif
