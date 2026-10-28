#include "Paths.h"

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#include <climits>
#endif

namespace iron3d {

std::filesystem::path exeDirectory() {
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path();
#elif defined(__linux__)
    char buffer[PATH_MAX]{};
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len <= 0) return std::filesystem::current_path();
    return std::filesystem::path(std::string(buffer, (size_t)len)).parent_path();
#else
    return std::filesystem::current_path();
#endif
}

} // namespace iron3d
