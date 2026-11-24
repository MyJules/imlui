// imlui_pack: builds a .pak archive (see tools/pack.cppm and cmake/ImluiResources.cmake).
import imlui.tools.pack;

int main(int argc, char** argv) { return imlui::tools::runPack(argc, argv); }
