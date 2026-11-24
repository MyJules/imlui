return {
    { prefix = "app", mount = "ui", base = ".", include = { "**/*.lua" }, exclude = { "main.lua" } },
    { prefix = "app", mount = "/images/", base = "images", include = "*.png", compress = { ["icon.png"] = false } },
    { prefix = "my-lib", files = "notes.txt", compress = { ["notes.txt"] = true } },
}
