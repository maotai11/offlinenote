# test/CMakeLists.txt
# catch2_vendored is defined in root CMakeLists.txt

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB2 REQUIRED glib-2.0>=2.56)
pkg_check_modules(GIO2 REQUIRED gio-2.0>=2.56)
pkg_check_modules(LIBXML2 REQUIRED libxml-2.0>=2.9)
pkg_check_modules(CAIRO REQUIRED cairo>=1.14)
pkg_check_modules(GDK_PIXBUF REQUIRED gdk-pixbuf-2.0>=2.36)
pkg_check_modules(POPPLER REQUIRED poppler-glib>=0.82)
pkg_check_modules(ZLIB REQUIRED zlib>=1.2)

function(add_onote_test name)
    add_executable(${name} ${ARGN} test_winmain_stub.cpp)
    target_link_libraries(${name} PRIVATE catch2_vendored offlinenote_core_lib)
    target_include_directories(${name} PRIVATE
        ${GLIB2_INCLUDE_DIRS}
        ${GIO2_INCLUDE_DIRS}
        ${LIBXML2_INCLUDE_DIRS}
        ${CAIRO_INCLUDE_DIRS}
        ${GDK_PIXBUF_INCLUDE_DIRS}
        ${POPPLER_INCLUDE_DIRS}
        ${ZLIB_INCLUDE_DIRS}
    )
    target_link_libraries(${name} PRIVATE
        ${GLIB2_LIBRARIES}
        ${GIO2_LIBRARIES}
        ${LIBXML2_LIBRARIES}
        ${CAIRO_LIBRARIES}
        ${GDK_PIXBUF_LIBRARIES}
        ${POPPLER_LIBRARIES}
        ${ZLIB_LIBRARIES}
    )
    # Ensure console subsystem on Windows — prevents linker from selecting WinMain
    if(WIN32)
        set_target_properties(${name} PROPERTIES
            WIN32_EXECUTABLE FALSE
            LINK_FLAGS "-Wl,-subsystem,console"
        )
    endif()
    add_test(NAME ${name} COMMAND ${name} WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
endfunction()

add_onote_test(test_document unit/test_document.cpp)
add_onote_test(test_stroke unit/test_stroke.cpp)
add_onote_test(test_serialization unit/test_serialization.cpp)
add_onote_test(test_app_controller unit/test_app_controller.cpp ../src/application/AppController.cpp)
add_onote_test(test_security_xxe unit/test_security_xxe.cpp)
add_onote_test(test_security_zipbomb unit/test_security_zipbomb.cpp)
add_onote_test(test_atomic_write unit/test_atomic_write.cpp)
add_onote_test(test_path_validator unit/test_path_validator.cpp)
add_onote_test(test_stroke_validation unit/test_stroke_validation.cpp)
add_onote_test(test_file_lock unit/test_file_lock.cpp)
add_onote_test(test_crash_recovery unit/test_crash_recovery.cpp)
add_onote_test(test_pdf_import_plan unit/test_pdf_import_plan.cpp)
add_onote_test(test_legacy_note_text_codec unit/test_legacy_note_text_codec.cpp)
add_onote_test(test_legacy_note_resource_helper unit/test_legacy_note_resource_helper.cpp)
