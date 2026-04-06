# cmake/Sanitizers.cmake
# AddressSanitizer / UBSan 開關
# SPDX-License-Identifier: GPL-2.0-or-later

function(enable_sanitizers)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        set(SANITIZER_FLAGS "-fsanitize=address,undefined -fno-omit-frame-pointer")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${SANITIZER_FLAGS}" PARENT_SCOPE)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SANITIZER_FLAGS}" PARENT_SCOPE)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${SANITIZER_FLAGS}" PARENT_SCOPE)
        message(STATUS "Sanitizers enabled: AddressSanitizer, UndefinedBehaviorSanitizer")
    else()
        message(WARNING "Sanitizers not supported on this compiler")
    endif()
endfunction()
