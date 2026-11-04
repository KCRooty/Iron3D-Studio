#include "FileDialog.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")
#endif

namespace iron3d {

#if defined(_WIN32)

namespace {
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
} // namespace

bool nativeFileDialogAvailable() { return true; }

std::optional<std::filesystem::path> openFileDialog(const std::string& title,
                                                    const std::vector<std::pair<std::string, std::string>>& filters) {
    // Formato de comdlg32: "Descripcion\0patrones\0...\0\0".
    std::wstring filter;
    for (const auto& f : filters) {
        filter += widen(f.first + " (" + f.second + ")");
        filter.push_back(L'\0');
        filter += widen(f.second);
        filter.push_back(L'\0');
    }
    filter.push_back(L'\0');
    const std::wstring wtitle = widen(title);

    wchar_t buffer[4096] = {0};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = filter.c_str();
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = (DWORD)(sizeof(buffer) / sizeof(buffer[0]));
    ofn.lpstrTitle = wtitle.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return std::nullopt;
    return std::filesystem::path(buffer);
}

#else

bool nativeFileDialogAvailable() { return false; }

std::optional<std::filesystem::path> openFileDialog(const std::string&,
                                                    const std::vector<std::pair<std::string, std::string>>&) {
    return std::nullopt;
}

#endif

} // namespace iron3d
