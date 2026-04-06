#include <filesystem>
bool atomicRename(const std::filesystem::path& src, const std::filesystem::path& dest) {
    std::error_code ec;
    std::filesystem::rename(src, dest, ec);
    return !ec;
}
