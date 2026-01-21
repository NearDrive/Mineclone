function(mineclone_enable_sanitizers target)
  if (NOT MINECLONE_SANITIZE)
    return()
  endif()

  if (CMAKE_BUILD_TYPE STREQUAL "Debug" AND
      (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()
