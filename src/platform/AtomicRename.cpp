#include "AtomicRename.h"

#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#endif

bool atomicRename(const std::filesystem::path& src, const std::filesystem::path& dest) {
    if (src.empty() || dest.empty()) {
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::exists(src, ec) || ec) {
        return false;
    }

#if defined(_WIN32)
    if (std::filesystem::exists(dest, ec) && !ec) {
        if (ReplaceFileW(dest.c_str(), src.c_str(), nullptr, 0, nullptr, nullptr) != 0) {
            return true;
        }
    }

    return MoveFileExW(
               src.c_str(),
               dest.c_str(),
               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::filesystem::rename(src, dest, ec);
    return !ec;
#endif
}
