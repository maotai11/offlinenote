# CMakeLists.txt — OfflineNote 根 CMake (v4.0 結構重構)
# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.20)

project(OfflineNote
    VERSION 4.0.2
    DESCRIPTION "Offline-first handwriting and PDF annotation desktop application"
    HOMEPAGE_URL "https://github.com/maotai11/offlinenote"
    LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(CompilerFlags)
include(DependencyVersions)
include(InstallDirs)

option(OFFLINENOTE_BUILD_TESTS "Build tests" ON)
option(OFFLINENOTE_ENABLE_ASAN "Enable AddressSanitizer" OFF)

# ──────────────────────────────────────────────
# 版本標頭
# ──────────────────────────────────────────────
configure_file(
    "${CMAKE_SOURCE_DIR}/src/util/Version.h.in"
    "${CMAKE_BINARY_DIR}/generated/Version.h"
    @ONLY
)

# ──────────────────────────────────────────────
# 依賴
# ──────────────────────────────────────────────
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0>=3.22)
pkg_check_modules(CAIRO REQUIRED cairo>=1.14)
pkg_check_modules(GLIB2 REQUIRED glib-2.0>=2.56)
pkg_check_modules(GDK_PIXBUF REQUIRED gdk-pixbuf-2.0>=2.36)
pkg_check_modules(LIBXML2 REQUIRED libxml-2.0>=2.9)
pkg_check_modules(ZLIB REQUIRED zlib>=1.2)
pkg_check_modules(POPPLER REQUIRED poppler-glib>=0.82)

if(OFFLINENOTE_ENABLE_ASAN)
    include(Sanitizers)
    enable_sanitizers()
endif()

# ──────────────────────────────────────────────
# 核心庫 (無 GTK 依賴，可供測試使用)
# ──────────────────────────────────────────────
add_library(offlinenote_core_lib STATIC)

target_sources(offlinenote_core_lib PRIVATE
    # Document
    src/document/Document.cpp
    src/document/Page.cpp
    src/document/Layer.cpp
    src/document/Stroke.cpp
    src/document/ImageElement.cpp

    # Serialization
    src/serialization/SecureXmlParser.cpp
    src/serialization/SafeDecompressor.cpp
    src/serialization/NoteSerializer.cpp
    src/serialization/NoteDeserializer.cpp
    src/serialization/XmlHelper.cpp
    src/serialization/MigrationManager.cpp

    # Rendering
    src/rendering/Renderer.cpp
    src/rendering/StrokeRenderer.cpp
    src/rendering/BackgroundRenderer.cpp
    src/rendering/PdfPageRenderer.cpp
    src/rendering/ImageRenderer.cpp
    src/rendering/RenderCache.cpp

    # Input
    src/input/InputRouter.cpp
    src/input/StylusHandler.cpp
    src/input/GestureHandler.cpp
    src/input/StrokeBuilder.cpp
    src/input/Stabilizer.cpp
    src/input/ToolState.cpp

    # Tools
    src/tools/ToolManager.cpp
    src/tools/PenTool.cpp
    src/tools/EraserTool.cpp
    src/tools/SelectionTool.cpp

    # Export
    src/export/ExportManager.cpp
    src/export/PdfExporter.cpp
    src/export/PngExporter.cpp

    # Import
    src/import/PdfImporter.cpp
    src/import/ImageImporter.cpp

    # Platform (無 GTK 依賴)
    src/platform/FontManager.cpp
    src/platform/ThemeManager.cpp
    src/platform/CrashLogger.cpp
    src/platform/FileLock.cpp
    src/platform/AtomicRename.cpp
    src/platform/PlatformWindows.cpp

    # Util
    src/util/Logger.cpp
    src/util/StringUtils.cpp
    src/util/FileUtils.cpp
    src/util/PathValidator.cpp
)

target_include_directories(offlinenote_core_lib PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_BINARY_DIR}/generated"
)

target_include_directories(offlinenote_core_lib PRIVATE
    ${GLIB2_INCLUDE_DIRS}
    ${LIBXML2_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
    ${CAIRO_INCLUDE_DIRS}
    ${GDK_PIXBUF_INCLUDE_DIRS}
    ${POPPLER_INCLUDE_DIRS}
)

target_link_libraries(offlinenote_core_lib PRIVATE
    ${GLIB2_LIBRARIES}
    ${LIBXML2_LIBRARIES}
    ${ZLIB_LIBRARIES}
    ${CAIRO_LIBRARIES}
    ${GDK_PIXBUF_LIBRARIES}
    ${POPPLER_LIBRARIES}
)

target_compile_options(offlinenote_core_lib PRIVATE
    ${GLIB2_CFLAGS_OTHER}
    ${LIBXML2_CFLAGS_OTHER}
)

# ──────────────────────────────────────────────
# 主要可執行檔
# ──────────────────────────────────────────────
add_executable(offlinenote
    src/main.cpp

    # Application (部分依賴 GTK)
    src/application/Application.cpp
    src/application/AppController.cpp
    src/application/Config.cpp
    src/application/PathManager.cpp
    src/application/StartupCheck.cpp

    # UI
    src/ui/MainWindow.cpp
    src/ui/MainToolbar.cpp
    src/ui/PageCanvas.cpp
    src/ui/MenuBar.cpp
    src/ui/StatusBar.cpp
    src/ui/dialogs/NewDocumentDialog.cpp
    src/ui/dialogs/ExportDialog.cpp
    src/ui/dialogs/AboutDialog.cpp
    src/ui/widgets/ColorButton.cpp
    src/ui/widgets/PenSizeSlider.cpp
    src/ui/widgets/ZoomController.cpp
)

target_include_directories(offlinenote PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_BINARY_DIR}/generated"
    ${GTK3_INCLUDE_DIRS}
    ${CAIRO_INCLUDE_DIRS}
    ${GLIB2_INCLUDE_DIRS}
    ${GDK_PIXBUF_INCLUDE_DIRS}
    ${LIBXML2_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
    ${POPPLER_INCLUDE_DIRS}
)

target_link_libraries(offlinenote PRIVATE
    offlinenote_core_lib
    ${GTK3_LIBRARIES}
    ${CAIRO_LIBRARIES}
    ${GLIB2_LIBRARIES}
    ${GDK_PIXBUF_LIBRARIES}
    ${LIBXML2_LIBRARIES}
    ${ZLIB_LIBRARIES}
    ${POPPLER_LIBRARIES}
)

# Windows: 不顯示 console 視窗
if(WIN32)
    set_target_properties(offlinenote PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# ──────────────────────────────────────────────
# 測試
# ──────────────────────────────────────────────
if(OFFLINENOTE_BUILD_TESTS)
    enable_testing()
    add_library(catch2_vendored STATIC test/catch_amalgamated.cpp)
    target_include_directories(catch2_vendored PUBLIC test)
    target_link_libraries(catch2_vendored PRIVATE ${GLIB2_LIBRARIES} ${LIBXML2_LIBRARIES})
    add_subdirectory(test)
endif()

# ──────────────────────────────────────────────
# 打包
# ──────────────────────────────────────────────
include(Packaging)
setup_cpack()
