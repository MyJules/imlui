/* The stb_image implementation, compiled into the stb_image library. Its configuration (PNG and JPEG only, no
   stdio, a size limit) comes from the library's compile definitions, so the wrapper module sees the same
   declarations (see external/CMakeLists.txt). */
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
