# Fetch and build PCRE2 for std.jdll (static 8-bit library).

include(FetchContent)

# pcre2 still declares an old cmake_minimum_required; quiet CMake 4+ noise.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(_jello_pcre2_diag_pushed FALSE)
set(_jello_pcre2_had_warn_deprecated FALSE)
if(COMMAND cmake_diagnostic)
  # CMake 4.4+: CMAKE_WARN_DEPRECATED is obsolete (CMP0218); use diagnostics.
  cmake_diagnostic(PUSH)
  cmake_diagnostic(SET CMD_DEPRECATED IGNORE)
  set(_jello_pcre2_diag_pushed TRUE)
else()
  if(DEFINED CMAKE_WARN_DEPRECATED)
    set(_jello_pcre2_had_warn_deprecated TRUE)
    set(_jello_pcre2_warn_deprecated "${CMAKE_WARN_DEPRECATED}")
  endif()
  set(CMAKE_WARN_DEPRECATED OFF)
endif()

# We only need the static 8-bit library — skip optional grep/test deps.
set(CMAKE_DISABLE_FIND_PACKAGE_BZip2 TRUE)
set(CMAKE_DISABLE_FIND_PACKAGE_ZLIB TRUE)
set(CMAKE_DISABLE_FIND_PACKAGE_Readline TRUE)
set(CMAKE_DISABLE_FIND_PACKAGE_Editline TRUE)

set(PCRE2_BUILD_PCRE2_8 ON CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2_16 OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2_32 OFF CACHE BOOL "" FORCE)
set(PCRE2_SUPPORT_JIT OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2GREP OFF CACHE BOOL "" FORCE)
set(PCRE2_SHOW_REPORT OFF CACHE BOOL "" FORCE)
set(PCRE2_SUPPORT_LIBBZ2 OFF CACHE BOOL "" FORCE)
set(PCRE2_SUPPORT_LIBZ OFF CACHE BOOL "" FORCE)
set(PCRE2_SUPPORT_LIBREADLINE OFF CACHE BOOL "" FORCE)
set(PCRE2_SUPPORT_LIBEDIT OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

set(_jello_pcre2_had_msg_level FALSE)
if(DEFINED CMAKE_MESSAGE_LOG_LEVEL)
  set(_jello_pcre2_had_msg_level TRUE)
  set(_jello_pcre2_msg_level "${CMAKE_MESSAGE_LOG_LEVEL}")
endif()
set(CMAKE_MESSAGE_LOG_LEVEL NOTICE)

FetchContent_Declare(
  pcre2
  GIT_REPOSITORY https://github.com/PCRE2Project/pcre2.git
  GIT_TAG pcre2-10.44
)

FetchContent_MakeAvailable(pcre2)

if(_jello_pcre2_had_msg_level)
  set(CMAKE_MESSAGE_LOG_LEVEL "${_jello_pcre2_msg_level}")
else()
  unset(CMAKE_MESSAGE_LOG_LEVEL)
endif()
if(_jello_pcre2_diag_pushed)
  cmake_diagnostic(POP)
elseif(_jello_pcre2_had_warn_deprecated)
  set(CMAKE_WARN_DEPRECATED "${_jello_pcre2_warn_deprecated}")
else()
  unset(CMAKE_WARN_DEPRECATED)
endif()
