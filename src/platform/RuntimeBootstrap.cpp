// SPDX-License-Identifier: GPL-2.0-or-later

#include "RuntimeBootstrap.h"
#include "../util/FileUtils.h"

#include <filesystem>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

#ifdef _WIN32
void setEnvPathIfExists(const wchar_t* name, const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return;
    }

    const std::wstring value = path.wstring();
    SetEnvironmentVariableW(name, value.c_str());
}

void prependExeDirToPath(const std::filesystem::path& exeDir) {
    const std::wstring exeDirWide = exeDir.wstring();
    DWORD required = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    std::wstring currentPath(required == 0 ? 0 : required, L'\0');

    if (required != 0) {
        GetEnvironmentVariableW(L"PATH", currentPath.data(), required);
        if (!currentPath.empty() && currentPath.back() == L'\0') {
            currentPath.pop_back();
        }
    }

    if (currentPath.rfind(exeDirWide + L";", 0) == 0 || currentPath == exeDirWide) {
        return;
    }

    const std::wstring updatedPath = currentPath.empty()
        ? exeDirWide
        : (exeDirWide + L";" + currentPath);
    SetEnvironmentVariableW(L"PATH", updatedPath.c_str());
}
#endif

} // namespace

void configureBundledRuntimeEnvironment() {
#ifdef _WIN32
    const std::filesystem::path exeDir = FileUtils::getExecutableDir();
    if (exeDir.empty()) {
        return;
    }

    prependExeDirToPath(exeDir);
    setEnvPathIfExists(L"GDK_PIXBUF_MODULEDIR", exeDir / "lib" / "gdk-pixbuf-2.0" / "2.10.0");
    setEnvPathIfExists(L"GDK_PIXBUF_MODULEFILE", exeDir / "lib" / "gdk-pixbuf-2.0" / "2.10.0" / "loaders.cache");
    setEnvPathIfExists(L"GSETTINGS_SCHEMA_DIR", exeDir / "share" / "glib-2.0" / "schemas");
    setEnvPathIfExists(L"XDG_DATA_DIRS", exeDir / "share");
    setEnvPathIfExists(L"FONTCONFIG_FILE", exeDir / "resources" / "fonts" / "fonts.conf");
#endif
}
