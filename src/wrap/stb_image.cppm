// Module wrapper around stb_image (external/stb/stb_image.h), built with only the PNG and JPEG decoders and no stdio
// (see external/CMakeLists.txt). Nothing else in the project includes the header.
module;

#include "stb_image.h"

export module stb_image;

export namespace stbi {

using ::stbi_failure_reason;
using ::stbi_image_free;
using ::stbi_info_from_memory;
using ::stbi_load_from_memory;
using ::stbi_uc;

} // namespace stbi
