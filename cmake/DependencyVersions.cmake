# cmake/DependencyVersions.cmake
# 所有外部依賴的版本約束
# SPDX-License-Identifier: GPL-2.0-or-later

# ── GTK3
set(DEP_GTK3_MIN_VERSION   "3.22.0")
set(DEP_GTK3_MAX_MAJOR     "3")
set(DEP_GTK3_PKG_NAME      "gtk+-3.0")

# ── Cairo
set(DEP_CAIRO_MIN_VERSION  "1.14.0")
set(DEP_CAIRO_PKG_NAME     "cairo")

# ── Poppler
set(DEP_POPPLER_MIN_VERSION "0.82.0")
set(DEP_POPPLER_TESTED_MAX_VERSION "26.99.0")
set(DEP_POPPLER_PKG_NAME   "poppler-glib")

# ── GDK-Pixbuf
set(DEP_GDK_PIXBUF_MIN_VERSION "2.36.0")
set(DEP_GDK_PIXBUF_PKG_NAME   "gdk-pixbuf-2.0")

# ── libxml2
set(DEP_LIBXML2_MIN_VERSION "2.9.0")
set(DEP_LIBXML2_PKG_NAME    "libxml-2.0")

# ── zlib
set(DEP_ZLIB_MIN_VERSION    "1.2.11")
set(DEP_ZLIB_PKG_NAME       "zlib")

# ──────────────────────────────────────────────
# 版本驗證函式
# ──────────────────────────────────────────────
function(check_dependency_version pkg_name found_version min_version max_major)
    if(found_version VERSION_LESS min_version)
        message(FATAL_ERROR
            "Dependency '${pkg_name}' version ${found_version} is too old.\n"
            "Minimum required: ${min_version}\n"
            "Please update the package."
        )
    endif()

    if(NOT max_major STREQUAL "")
        string(REGEX MATCH "^([0-9]+)" found_major "${found_version}")
        if(found_major GREATER max_major)
            message(WARNING
                "Dependency '${pkg_name}' version ${found_version} has major version "
                "${found_major}, but only major version ${max_major} has been tested.\n"
                "The application may not work correctly. "
                "Please report any issues."
            )
        endif()
    endif()
endfunction()
