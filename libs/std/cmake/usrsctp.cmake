# Fetch and build usrsctp for std.jdll (static library, all platforms).

include(FetchContent)

# usrsctp still declares cmake_minimum_required(3.0); allow/quiet it under CMake 4+.
if(POLICY CMP0157)
  cmake_policy(SET CMP0157 NEW)
endif()
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(_jello_usrsctp_diag_pushed FALSE)
set(_jello_usrsctp_had_warn_deprecated FALSE)
if(COMMAND cmake_diagnostic)
  # CMake 4.4+: CMAKE_WARN_DEPRECATED is obsolete (CMP0218); use diagnostics.
  cmake_diagnostic(PUSH)
  cmake_diagnostic(SET CMD_DEPRECATED IGNORE)
  set(_jello_usrsctp_diag_pushed TRUE)
else()
  if(DEFINED CMAKE_WARN_DEPRECATED)
    set(_jello_usrsctp_had_warn_deprecated TRUE)
    set(_jello_usrsctp_warn_deprecated "${CMAKE_WARN_DEPRECATED}")
  endif()
  set(CMAKE_WARN_DEPRECATED OFF)
endif()

set(sctp_build_programs OFF CACHE BOOL "" FORCE)
set(sctp_build_shared_lib OFF CACHE BOOL "" FORCE)
set(sctp_build_fuzzer OFF CACHE BOOL "" FORCE)
set(sctp_werror OFF CACHE BOOL "" FORCE)
set(sctp_debug OFF CACHE BOOL "" FORCE)
set(sctp_inet ON CACHE BOOL "" FORCE)
set(sctp_inet6 ON CACHE BOOL "" FORCE)

set(_jello_usrsctp_had_msg_level FALSE)
if(DEFINED CMAKE_MESSAGE_LOG_LEVEL)
  set(_jello_usrsctp_had_msg_level TRUE)
  set(_jello_usrsctp_msg_level "${CMAKE_MESSAGE_LOG_LEVEL}")
endif()
set(CMAKE_MESSAGE_LOG_LEVEL NOTICE)

FetchContent_Declare(
  usrsctp
  GIT_REPOSITORY https://github.com/sctplab/usrsctp.git
  GIT_TAG 0.9.5.0
)

FetchContent_MakeAvailable(usrsctp)

if(_jello_usrsctp_had_msg_level)
  set(CMAKE_MESSAGE_LOG_LEVEL "${_jello_usrsctp_msg_level}")
else()
  unset(CMAKE_MESSAGE_LOG_LEVEL)
endif()
if(_jello_usrsctp_diag_pushed)
  cmake_diagnostic(POP)
elseif(_jello_usrsctp_had_warn_deprecated)
  set(CMAKE_WARN_DEPRECATED "${_jello_usrsctp_warn_deprecated}")
else()
  unset(CMAKE_WARN_DEPRECATED)
endif()

# Upstream is noisy under GCC (-Wunused-but-set-variable, -Wmaybe-uninitialized).
if(TARGET usrsctp AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(usrsctp PRIVATE
    -Wno-unused-but-set-variable
    -Wno-maybe-uninitialized
    -Wno-unused-parameter
  )
endif()
