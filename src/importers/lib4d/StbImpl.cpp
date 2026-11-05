// Implementacion de stb_image (dominio publico / MIT): JPEG para las
// miniaturas de las librerias .lib4d, PNG y el descompresor zlib que trae
// dentro (stbi_zlib_decode_buffer) para los datos de cada elemento.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>
