local ok = pcall(function() return io.open("x") end)
assert(not ok)
assert(load == nil and dofile == nil and loadfile == nil and require == nil and os == nil)
return { prefix = "app", files = "main.lua", compress = { ["main.lua"] = "yes" } }
