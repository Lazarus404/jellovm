# Resolve OpenSSL for std.jdll.
#
# Prefer a system install. On Windows, if none is found, download the MSYS2
# UCRT MinGW OpenSSL package (matches WinLibs UCRT) into the build tree.

find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
  message(STATUS "OpenSSL: ${OPENSSL_VERSION} (system)")
  return()
endif()

if(NOT WIN32)
  find_package(OpenSSL REQUIRED)
  return()
endif()

set(_jello_openssl_pkg_ver "3.5.1-1")
set(_jello_openssl_pkg
  "mingw-w64-ucrt-x86_64-openssl-${_jello_openssl_pkg_ver}-any.pkg.tar.zst")
set(_jello_openssl_url
  "https://mirror.msys2.org/mingw/ucrt64/${_jello_openssl_pkg}")
set(_jello_openssl_root "${CMAKE_BINARY_DIR}/_deps/openssl-msys")
set(_jello_openssl_prefix "${_jello_openssl_root}/ucrt64")
set(_jello_openssl_stamp "${_jello_openssl_prefix}/.extracted")

find_program(_jello_win_tar NAMES tar
  PATHS "$ENV{SystemRoot}/System32" NO_DEFAULT_PATH)
if(NOT _jello_win_tar)
  find_program(_jello_win_tar NAMES tar)
endif()

find_program(_jello_zstd NAMES zstd
  PATHS
    "${CMAKE_BINARY_DIR}/_deps/zstd/zstd-v1.5.6-win64"
    "${CMAKE_BINARY_DIR}/_deps/zstd"
  NO_DEFAULT_PATH
)
if(NOT _jello_zstd)
  find_program(_jello_zstd NAMES zstd)
endif()

if(NOT EXISTS "${_jello_openssl_stamp}")
  message(STATUS "OpenSSL not found; downloading MSYS2 UCRT package ${_jello_openssl_pkg_ver}…")
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/_deps")

  if(NOT _jello_zstd)
    set(_jello_zstd_zip "${CMAKE_BINARY_DIR}/_deps/zstd-win64.zip")
    set(_jello_zstd_dir "${CMAKE_BINARY_DIR}/_deps/zstd")
    file(DOWNLOAD
      "https://github.com/facebook/zstd/releases/download/v1.5.6/zstd-v1.5.6-win64.zip"
      "${_jello_zstd_zip}"
      SHOW_PROGRESS
      STATUS _jello_zstd_dl
      TLS_VERIFY ON
    )
    list(GET _jello_zstd_dl 0 _jello_zstd_dl_code)
    if(NOT _jello_zstd_dl_code EQUAL 0)
      message(FATAL_ERROR "Failed to download zstd (needed to unpack OpenSSL)")
    endif()
    file(REMOVE_RECURSE "${_jello_zstd_dir}")
    file(MAKE_DIRECTORY "${_jello_zstd_dir}")
    execute_process(
      COMMAND "${_jello_win_tar}" -xf "${_jello_zstd_zip}"
      WORKING_DIRECTORY "${_jello_zstd_dir}"
      RESULT_VARIABLE _jello_zstd_untar
    )
    if(NOT _jello_zstd_untar EQUAL 0)
      message(FATAL_ERROR "Failed to extract zstd.zip (use System32 tar.exe, not Git tar)")
    endif()
    file(GLOB_RECURSE _jello_zstd_candidates "${_jello_zstd_dir}/zstd.exe")
    list(LENGTH _jello_zstd_candidates _jello_zstd_n)
    if(_jello_zstd_n EQUAL 0)
      message(FATAL_ERROR "zstd.exe missing after extract")
    endif()
    list(GET _jello_zstd_candidates 0 _jello_zstd)
  endif()

  set(_jello_openssl_zst "${CMAKE_BINARY_DIR}/_deps/${_jello_openssl_pkg}")
  set(_jello_openssl_tar "${CMAKE_BINARY_DIR}/_deps/mingw-openssl.pkg.tar")
  file(DOWNLOAD
    "${_jello_openssl_url}"
    "${_jello_openssl_zst}"
    SHOW_PROGRESS
    STATUS _jello_openssl_dl
    TLS_VERIFY ON
  )
  list(GET _jello_openssl_dl 0 _jello_openssl_dl_code)
  if(NOT _jello_openssl_dl_code EQUAL 0)
    list(GET _jello_openssl_dl 1 _jello_openssl_dl_msg)
    message(FATAL_ERROR
      "Failed to download OpenSSL (${_jello_openssl_dl_msg}).\n"
      "Set OPENSSL_ROOT_DIR to a MinGW/UCRT OpenSSL and reconfigure.")
  endif()

  execute_process(
    COMMAND "${_jello_zstd}" -d "${_jello_openssl_zst}" -o "${_jello_openssl_tar}" -f
    RESULT_VARIABLE _jello_openssl_zstd
  )
  if(NOT _jello_openssl_zstd EQUAL 0)
    message(FATAL_ERROR "zstd decompress of OpenSSL package failed")
  endif()

  file(REMOVE_RECURSE "${_jello_openssl_root}")
  file(MAKE_DIRECTORY "${_jello_openssl_root}")
  execute_process(
    COMMAND "${_jello_win_tar}" -xf "${_jello_openssl_tar}"
    WORKING_DIRECTORY "${_jello_openssl_root}"
    RESULT_VARIABLE _jello_openssl_untar
  )
  if(NOT _jello_openssl_untar EQUAL 0)
    message(FATAL_ERROR "Failed to extract OpenSSL package")
  endif()
  if(NOT EXISTS "${_jello_openssl_prefix}/include/openssl/ssl.h")
    message(FATAL_ERROR "OpenSSL headers missing under ${_jello_openssl_prefix}")
  endif()
  file(WRITE "${_jello_openssl_stamp}" "${_jello_openssl_url}\n")
endif()

# Force the static archives (gcc otherwise prefers libssl.dll.a).
set(OPENSSL_ROOT_DIR "${_jello_openssl_prefix}" CACHE PATH "OpenSSL root" FORCE)
set(OPENSSL_INCLUDE_DIR "${_jello_openssl_prefix}/include" CACHE PATH "" FORCE)
set(OPENSSL_SSL_LIBRARY "${_jello_openssl_prefix}/lib/libssl.a" CACHE FILEPATH "" FORCE)
set(OPENSSL_CRYPTO_LIBRARY "${_jello_openssl_prefix}/lib/libcrypto.a" CACHE FILEPATH "" FORCE)
set(OPENSSL_USE_STATIC_LIBS TRUE)
unset(OpenSSL_DIR CACHE)

find_package(OpenSSL REQUIRED)
message(STATUS "OpenSSL: ${OPENSSL_VERSION} (MSYS2 UCRT at ${OPENSSL_ROOT_DIR})")

# Expose DLL paths for optional POST_BUILD copy (runtime deps if anything pulls shared).
set(JELLO_OPENSSL_BIN_DIR "${_jello_openssl_prefix}/bin" CACHE PATH "" FORCE)
