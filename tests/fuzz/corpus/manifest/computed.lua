local files = {}
for _, name in ipairs { "main", "components/Tile" } do files[#files + 1] = name .. ".lua" end
local group = setmetatable({ prefix = "app" }, { __index = function() error("no") end })
group.files = files
return group
