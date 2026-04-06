#include <filesystem>
class Document; class ExportManager { public: bool exportPdf(const Document&, const std::filesystem::path&) { return true; } bool exportPng(const Document&, const std::filesystem::path&) { return true; } };
