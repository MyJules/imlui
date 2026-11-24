-- examples/counter as an archive mounted at app:/counter/ (imlui_add_pak in tests/CMakeLists.txt), read back by
-- resources_packed_test.
return {
    prefix = "app",
    mount = "counter",
    base = "../../examples/counter",
    include = "**",
}
