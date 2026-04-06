# cmake/InstallDirs.cmake
# 安裝路徑與 portable mode 偵測
# SPDX-License-Identifier: GPL-2.0-or-later

include(GNUInstallDirs)

# ── 安裝路徑定義
if(WIN32)
    set(OFFLINENOTE_INSTALL_BIN_DIR ".")
    set(OFFLINENOTE_INSTALL_RESOURCE_DIR "resources")
    set(OFFLINENOTE_INSTALL_LIB_DIR "lib")
    set(OFFLINENOTE_INSTALL_SHARE_DIR "share")
elseif(APPLE)
    set(OFFLINENOTE_INSTALL_BIN_DIR "bin")
    set(OFFLINENOTE_INSTALL_RESOURCE_DIR "share/offlinenote/resources")
    set(OFFLINENOTE_INSTALL_LIB_DIR "lib")
    set(OFFLINENOTE_INSTALL_SHARE_DIR "share")
else()
    set(OFFLINENOTE_INSTALL_BIN_DIR "${CMAKE_INSTALL_BINDIR}")
    set(OFFLINENOTE_INSTALL_RESOURCE_DIR "${CMAKE_INSTALL_DATADIR}/offlinenote/resources")
    set(OFFLINENOTE_INSTALL_LIB_DIR "${CMAKE_INSTALL_LIBDIR}")
    set(OFFLINENOTE_INSTALL_SHARE_DIR "${CMAKE_INSTALL_DATADIR}")
endif()

# ── 資源安裝函式
function(install_resources)
    # 字型資源
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/resources/fonts/"
            DESTINATION "${OFFLINENOTE_INSTALL_RESOURCE_DIR}/fonts")

    # 圖示資源
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/resources/icons/"
            DESTINATION "${OFFLINENOTE_INSTALL_RESOURCE_DIR}/icons")

    # 主題資源
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/resources/themes/"
            DESTINATION "${OFFLINENOTE_INSTALL_RESOURCE_DIR}/themes")

    # 翻譯資源
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/resources/translations/"
            DESTINATION "${OFFLINENOTE_INSTALL_RESOURCE_DIR}/translations")

    # 頁面模板
    if(EXISTS "${CMAKE_SOURCE_DIR}/resources-templates")
        install(DIRECTORY "${CMAKE_SOURCE_DIR}/resources-templates/"
                DESTINATION "${OFFLINENOTE_INSTALL_RESOURCE_DIR}/resources-templates")
    endif()
endfunction()
