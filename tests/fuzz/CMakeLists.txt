# Fuzz targets for the code that reads untrusted bytes (docs/development.md#fuzzing). Each target is one file,
# <name>.cpp, defining LLVMFuzzerTestOneInput, built two ways:
#
#   imlui_fuzz_replay_<name>  always: with replay_main.cpp, which feeds it every file of its seed corpus
#                             (corpus/<name>/, plus the SEEDS given below) once. The ctest test fuzz.<name> (label
#                             `fuzz`) runs it, so every test run replays the corpora, crash regressions included.
#   imlui_fuzz_<name>         with IMLUI_FUZZ=ON (the clang-fuzz preset), when the compiler has libFuzzer: the fuzzer
#                             itself (-fsanitize=fuzzer), to run by hand, e.g.
#                             `imlui_fuzz_pak -max_total_time=120 <work corpus> tests/fuzz/corpus/pak`.

# imlui.fuzz.truetype: stb_truetype read the way ImGui reads a font (the `font` target). ImGui's copy is static inside
# imgui_draw.cpp, so the module compiles its own from ImGui's imstb_truetype.h (ext::imgui's include folder).
add_library(imlui_fuzz_support STATIC)
target_sources(imlui_fuzz_support
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/support
        FILES support/truetype.cppm
)
target_link_libraries(imlui_fuzz_support PUBLIC imlui_lua PRIVATE ext::imgui)
imlui_set_warnings(imlui_fuzz_support)
imlui_set_hardening(imlui_fuzz_support)

# imlui_add_fuzz_target(<name> LIBRARIES <libs...> [DEFINITIONS <defs...>] [SEEDS <files or folders...>])
#   SEEDS: more inputs for the replay test, besides corpus/<name>/: files too large to check in, or built.
function(imlui_add_fuzz_target name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "LIBRARIES;DEFINITIONS;SEEDS")
    set(targets imlui_fuzz_replay_${name})
    add_executable(imlui_fuzz_replay_${name} replay_main.cpp ${name}.cpp)
    if(IMLUI_FUZZ_SUPPORTED)
        add_executable(imlui_fuzz_${name} ${name}.cpp)
        target_link_options(imlui_fuzz_${name} PRIVATE ${IMLUI_FUZZ_LINK_OPTIONS})
        list(APPEND targets imlui_fuzz_${name})
    endif()
    foreach(target IN LISTS targets)
        target_link_libraries(${target} PRIVATE ${arg_LIBRARIES})
        target_compile_definitions(${target} PRIVATE ${arg_DEFINITIONS})
        imlui_set_warnings(${target})
        imlui_set_hardening(${target})
    endforeach()
    add_test(NAME fuzz.${name}
             COMMAND imlui_fuzz_replay_${name} ${CMAKE_CURRENT_SOURCE_DIR}/corpus/${name} ${arg_SEEDS})
    set_tests_properties(fuzz.${name} PROPERTIES LABELS fuzz TIMEOUT 300)
endfunction()

# .pak archives: parsePak, PakMount::fromBytes, reading every file (zstd). Seeds: the smoke app's archive
# (tests/CMakeLists.txt, imlui_test_pak).
imlui_add_fuzz_target(pak LIBRARIES imlui_core SEEDS ${IMLUI_TEST_PAK_FILE})
add_dependencies(imlui_fuzz_replay_pak imlui_test_pak)

# Fonts: checkFont, then stb_truetype on what it accepts. Seeds: the bundled fonts (TrueType and CFF outlines).
imlui_add_fuzz_target(font LIBRARIES imlui_core imlui_fuzz_support
                      SEEDS ${PROJECT_SOURCE_DIR}/external/fonts/Roboto-Regular.ttf
                            ${PROJECT_SOURCE_DIR}/external/fonts/MaterialIconsRound-Regular.otf)

# Resource manifests: the Lua sandbox, then the groups resolved against manifest_root/.
imlui_add_fuzz_target(manifest LIBRARIES imlui_resource_tools
                      DEFINITIONS IMLUI_FUZZ_MANIFEST_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/manifest_root")

# PNG and JPEG decoding (stb_image).
imlui_add_fuzz_target(image LIBRARIES imlui_core)
