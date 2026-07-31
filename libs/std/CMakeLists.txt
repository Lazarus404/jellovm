# std.jdll — Neko std.ndll subset

include("${CMAKE_CURRENT_LIST_DIR}/cmake/pcre2.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/usrsctp.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/openssl.cmake")

jello_jdll_embed_abi("${CMAKE_CURRENT_SOURCE_DIR}/std.jdll.abi" _std_abi_embed_c)

add_library(std_jdll SHARED
  init.c
  core/concat.c
  core/unicode.c
  core/regexp.c
  crypto/random.c
  crypto/md5.c
  sys/sys.c
  sys/process.c
  sys/date.c
  sys/power.c
  sys/thermal.c
  io/file.c
  io/io_common.c
  io/poll.c
  io/net/net.c
  io/net/tls_crypto.c
  io/net/tls_io.c
  io/net/sctp.c
  concurrency/thread.c
  concurrency/channel.c
  concurrency/task.c
  "${_std_abi_embed_c}"
)
target_include_directories(std_jdll PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../src/include"
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/core"
  "${CMAKE_CURRENT_SOURCE_DIR}/crypto"
  "${CMAKE_CURRENT_SOURCE_DIR}/sys"
  "${CMAKE_CURRENT_SOURCE_DIR}/io"
  "${CMAKE_CURRENT_SOURCE_DIR}/io/net"
  "${CMAKE_CURRENT_SOURCE_DIR}/concurrency"
  "${pcre2_SOURCE_DIR}/src"
  "${usrsctp_SOURCE_DIR}/usrsctplib"
)

target_link_libraries(std_jdll PRIVATE pcre2-8 OpenSSL::SSL OpenSSL::Crypto usrsctp)
if(WIN32)
  target_link_libraries(std_jdll PRIVATE ws2_32 crypt32 bcrypt)
endif()

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
  find_library(JELLO_IOKIT IOKit REQUIRED)
  find_library(JELLO_COREFOUNDATION CoreFoundation REQUIRED)
  target_link_libraries(std_jdll PRIVATE ${JELLO_IOKIT} ${JELLO_COREFOUNDATION})
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
if(WIN32 AND JELLO_OPENSSL_BIN_DIR)
  foreach(_dll IN ITEMS libssl-3-x64.dll libcrypto-3-x64.dll)
    if(EXISTS "${JELLO_OPENSSL_BIN_DIR}/${_dll}")
      add_custom_command(TARGET std_jdll POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${JELLO_OPENSSL_BIN_DIR}/${_dll}"
                "$<TARGET_FILE_DIR:std_jdll>/${_dll}"
        COMMENT "Copy ${_dll} next to std.jdll"
      )
    endif()
  endforeach()
endif()
