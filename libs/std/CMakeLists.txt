# std.jdll — Neko std.ndll subset

include("${CMAKE_CURRENT_LIST_DIR}/pcre2.cmake")

jello_jdll_embed_abi("${CMAKE_CURRENT_SOURCE_DIR}/std.jdll.abi" _std_abi_embed_c)

add_library(std_jdll SHARED
  init.c sys.c file.c process.c thread.c channel.c task.c random.c md5.c concat.c date.c unicode.c regexp.c
  "${_std_abi_embed_c}"
)
target_include_directories(std_jdll PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../src/include"
  "${pcre2_SOURCE_DIR}/src"
)

target_link_libraries(std_jdll PRIVATE pcre2-8)

set_target_properties(std_jdll PROPERTIES
  OUTPUT_NAME "std"
  PREFIX ""
  SUFFIX ".jdll"
)
if(WIN32)
  # PE treats .jdll as a runtime (DLL) artifact; keep it in lib/ with the .abi sidecar.
  set_target_properties(std_jdll PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
  )
endif()

if(APPLE)
  target_link_options(std_jdll PRIVATE "-undefined" "dynamic_lookup")
elseif(MINGW)
  if(JELLOVM_SHARED)
    # Link against jellovm.dll; PE cannot defer jdl_* like ELF -undefined dynamic_lookup.
    target_link_libraries(std_jdll PRIVATE jellovm)
  else()
    message(FATAL_ERROR "std.jdll on MinGW requires -DJELLOVM_SHARED=ON")
  endif()
elseif(UNIX)
  target_link_options(std_jdll PRIVATE "-Wl,--allow-shlib-undefined")
endif()

add_custom_command(TARGET std_jdll POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/std.jdll.abi"
          "$<TARGET_FILE_DIR:std_jdll>/std.jdll.abi"
  COMMENT "Copy std.jdll.abi next to std.jdll"
)
