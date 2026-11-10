#include "TextureImage.h"

#include <stb_image.h>

#include <filesystem>
#include <fstream>
#include <iterator>

namespace iron3d {

bool loadTextureImage(const std::string& path, TextureImage& out) {
    out = {};
    if (path.empty()) return false;
    std::ifstream f(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    if (!f) return false;
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (bytes.empty()) return false;
    int w = 0, h = 0, comp = 0;
    stbi_uc* data = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &comp, 4);
    if (!data) return false;
    out.width = w;
    out.height = h;
    out.rgba.assign(data, data + (size_t)w * (size_t)h * 4);
    stbi_image_free(data);
    return w > 0 && h > 0;
}

std::string textureCacheKey(const std::string& rawPath, const std::string& baseDir) {
    return rawPath + '\n' + baseDir;
}

} // namespace iron3d
