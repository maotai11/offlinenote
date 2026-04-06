#include <filesystem>
class FontManager { public: static FontManager& instance() { static FontManager m; return m; } void initialize(const std::filesystem::path&) {} };
