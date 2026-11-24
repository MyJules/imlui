# Examples that have C++ code. Each one is a self-contained folder with its own CMakeLists.txt and executable.
# Lua-only examples (gallery, counter) need no build: imlui_app copies them next to its binary (and embeds the
# gallery, its default app). Examples with C++ embed their own scripts with imlui_add_resources.
add_subdirectory(custom_types)
