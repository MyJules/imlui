// imlui_pack: builds a .pak archive (see tools/pack.cppm and cmake/ImluiResources.cmake).
import imlui.tools.pack;
import imlui.tools.source_files;

// wmain on Windows: main()'s argv is in the ANSI code page, which loses characters outside it (toolMain()).
#if defined(_WIN32)
int wmain(int argc, wchar_t** argv) { return imlui::tools::toolMain("imlui_pack", argc, argv, imlui::tools::runPack); }
#else
int main(int argc, char** argv) { return imlui::tools::toolMain("imlui_pack", argc, argv, imlui::tools::runPack); }
#endif
