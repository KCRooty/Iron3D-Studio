// Implementacion de stb_image (dominio publico / MIT): JPEG para las
// miniaturas de las librerias .lib4d, PNG y el descompresor zlib que trae
// dentro (stbi_zlib_decode_buffer) para los datos de cada elemento.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
// Texturas de materiales (2026-11-10): en las .lib4d reales salen tambien
// BMP y TGA, y PSD es habitual en escenas de C4D.
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_ONLY_PSD
#define STBI_NO_STDIO
#include <stb_image.h>

// stb_image_write: JPEG (miniaturas de las pruebas) y su compresor zlib
// (stbi_zlib_compress) para los modelos que se guardan en una .lib4d.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>
