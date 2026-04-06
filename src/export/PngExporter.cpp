#include <filesystem>
class PngExporter { public: bool exportPage(void*, const std::filesystem::path&, int) { return true; } };
