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

std::vector<std::filesystem::path> openFilesDialog(const std::string& title,
                                                   const std::vector<std::pair<std::string, std::string>>& filters) {
    std::wstring filter;
    for (const auto& f : filters) {
        filter += widen(f.first + " (" + f.second + ")");
        filter.push_back(L'\0');
        filter += widen(f.second);
        filter.push_back(L'\0');
    }
    filter.push_back(L'\0');
    const std::wstring wtitle = widen(title);
    // Con varios: "carpeta\0archivo1\0archivo2\0\0"; con uno: la ruta completa.
    std::vector<wchar_t> buffer(1 << 16, L'\0');
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = filter.c_str();
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = (DWORD)buffer.size();
    ofn.lpstrTitle = wtitle.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER | OFN_ALLOWMULTISELECT;
    std::vector<std::filesystem::path> out;
    if (!GetOpenFileNameW(&ofn)) return out;
    const wchar_t* p = buffer.data();
    const std::filesystem::path first(p);
    p += wcslen(p) + 1;
    if (!*p) {
        out.push_back(first);
        return out;
    }
    for (; *p; p += wcslen(p) + 1) out.push_back(first / p);
    return out;
}

std::optional<std::filesystem::path> saveFileDialog(const std::string& title,
                                                    const std::vector<std::pair<std::string, std::string>>& filters,
                                                    const std::string& defaultName, const std::string& defaultExt) {
    std::wstring filter;
    for (const auto& f : filters) {
        filter += widen(f.first + " (" + f.second + ")");
        filter.push_back(L'\0');
        filter += widen(f.second);
        filter.push_back(L'\0');
    }
    filter.push_back(L'\0');
    const std::wstring wtitle = widen(title);
    const std::wstring wext = widen(defaultExt);
    std::vector<wchar_t> buffer(4096, L'\0');
    // Nombre propuesto, sin los caracteres que Windows no admite.
    size_t n = 0;
    for (wchar_t c : widen(defaultName)) {
        if (n + 1 >= buffer.size()) break;
        buffer[n++] = (c < 32 || wcschr(L"<>:\"/\\|?*", c)) ? L'_' : c;
    }
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = filter.c_str();
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = (DWORD)buffer.size();
    ofn.lpstrTitle = wtitle.c_str();
    ofn.lpstrDefExt = wext.empty() ? nullptr : wext.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetSaveFileNameW(&ofn)) return std::nullopt;
    return std::filesystem::path(buffer.data());
}

#else

bool nativeFileDialogAvailable() { return false; }

std::optional<std::filesystem::path> saveFileDialog(const std::string&,
                                                    const std::vector<std::pair<std::string, std::string>>&,
                                                    const std::string&, const std::string&) {
    return std::nullopt;
}

std::vector<std::filesystem::path> openFilesDialog(const std::string&,
                                                   const std::vector<std::pair<std::string, std::string>>&) {
    return {};
}

std::optional<std::filesystem::path> openFileDialog(const std::string&,
                                                    const std::vector<std::pair<std::string, std::string>>&) {
    return std::nullopt;
}

#endif

} // namespace iron3d
