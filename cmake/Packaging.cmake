# cmake/Packaging.cmake
# CPack 設定（deb/rpm/NSIS/dmg）
# SPDX-License-Identifier: GPL-2.0-or-later

function(setup_cpack)
    set(CPACK_PACKAGE_NAME "OfflineNote")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
    set(CPACK_PACKAGE_VENDOR "OfflineNote Contributors")
    set(CPACK_PACKAGE_CONTACT "example@example.com")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
    set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")
    set(CPACK_MONOLITHIC_INSTALL TRUE)
    set(CPACK_VERBATIM_VARIABLES TRUE)
    set(CPACK_PACKAGE_CHECKSUM "SHA256")
    set(CPACK_PACKAGE_EXECUTABLES "offlinenote" "OfflineNote")

    # ── Generic settings
    set(CPACK_GENERATOR "ZIP")

    if(WIN32)
        list(APPEND CPACK_GENERATOR "NSIS")
        set(CPACK_NSIS_DISPLAY_NAME "OfflineNote")
        set(CPACK_NSIS_PACKAGE_NAME "OfflineNote")
        set(CPACK_NSIS_CONTACT "${CPACK_PACKAGE_CONTACT}")
    elseif(APPLE)
        list(APPEND CPACK_GENERATOR "DragNDrop")
        set(CPACK_DMG_VOLUME_NAME "OfflineNote")
    else()
        list(APPEND CPACK_GENERATOR "DEB" "RPM")

        # DEB settings
        set(CPACK_DEBIAN_PACKAGE_DEPENDS
            "libgtk-3-0 (>= 3.22), libcairo2, libpoppler-glib8, libgdk-pixbuf2.0-0, libxml2, zlib1g")
        set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "fonts-noto-core")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")

        # RPM settings
        set(CPACK_RPM_PACKAGE_REQUIRES
            "gtk3 >= 3.22, cairo, poppler-glib, gdk-pixbuf2, libxml2, zlib")
        set(CPACK_RPM_PACKAGE_ARCHITECTURE "x86_64")
    endif()

    include(CPack)
endfunction()
