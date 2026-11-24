// imlui_rc: embeds resource files into a C++ module (see tools/lib/rc.cppm and cmake/ImluiResources.cmake).
import imlui.tools.rc;

int main(int argc, char** argv) { return imlui::tools::runRc(argc, argv); }
