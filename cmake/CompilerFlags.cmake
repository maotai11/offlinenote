# cmake/CompilerFlags.cmake
# 跨平台編譯器旗標設定
# SPDX-License-Identifier: GPL-2.0-or-later

# ── 通用警告旗標
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
)

# ── 平台特定旗標
if(MSVC)
    add_compile_options(
        /W4
        /utf-8
    )
else()
    add_compile_options(
        -Werror=return-type
        -fstack-protector-strong
    )
endif()

# ── 發布模式優化
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")

# ── 除錯模式
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -DDEBUG")
