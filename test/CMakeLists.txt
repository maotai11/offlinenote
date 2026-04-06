# test/CMakeLists.txt
# catch2_vendored is defined in root CMakeLists.txt

function(add_onote_test name sources)
    add_executable(${name} ${sources})
    target_link_libraries(${name} PRIVATE catch2_vendored offlinenote_core_lib)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_onote_test(test_document unit/test_document.cpp)
add_onote_test(test_stroke unit/test_stroke.cpp)
add_onote_test(test_serialization unit/test_serialization.cpp)
add_onote_test(test_security_xxe unit/test_security_xxe.cpp)
add_onote_test(test_security_zipbomb unit/test_security_zipbomb.cpp)
add_onote_test(test_atomic_write unit/test_atomic_write.cpp)
add_onote_test(test_path_validator unit/test_path_validator.cpp)
add_onote_test(test_stroke_validation unit/test_stroke_validation.cpp)
add_onote_test(test_file_lock unit/test_file_lock.cpp)
