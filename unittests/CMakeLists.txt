add_custom_target(GobackendUnitTests)
set_target_properties(GobackendUnitTests PROPERTIES FOLDER "Tests")

if (APPLE)
  set(CMAKE_INSTALL_RPATH "@executable_path/../../lib")
else(UNIX)
  set(CMAKE_INSTALL_RPATH "\$ORIGIN/../../lib${LLVM_LIBDIR_SUFFIX}")
endif()

function(add_gobackend_unittest test_dirname)
  add_unittest(GobackendUnitTests ${test_dirname} ${ARGN})
endfunction()

add_subdirectory(BackendCore)
add_subdirectory(Parser)
