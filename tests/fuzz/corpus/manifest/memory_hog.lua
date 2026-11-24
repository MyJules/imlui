local t = {}
local s = "x"
for i = 1, 40 do s = s .. s; t[i] = s end
return t
