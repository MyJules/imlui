// imlui_rc: embeds resource files into a C++ module (see tools/lib/rc.cppm and cmake/ImluiResources.cmake).
import imlui.tools.rc;
import imlui.tools.source_files;

// wmain on Windows: main()'s argv is in the ANSI code page, which loses characters outside it (toolMain()).
#if defined(_WIN32)
int wmain(int argc, wchar_t** argv) { return imlui::tools::toolMain("imlui_rc", argc, argv, imlui::tools::runRc); }
#else
int main(int argc, char** argv) { return imlui::tools::toolMain("imlui_rc", argc, argv, imlui::tools::runRc); }
#endif
