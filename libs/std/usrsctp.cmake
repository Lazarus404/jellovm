# Fetch and build usrsctp for std.jdll (static library, all platforms).

include(FetchContent)

# usrsctp still declares cmake_minimum_required(3.0); allow it under CMake 4+.
if(POLICY CMP0157)
  cmake_policy(SET CMP0157 NEW)
endif()
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

set(sctp_build_programs OFF CACHE BOOL "" FORCE)
set(sctp_build_shared_lib OFF CACHE BOOL "" FORCE)
set(sctp_build_fuzzer OFF CACHE BOOL "" FORCE)
set(sctp_werror OFF CACHE BOOL "" FORCE)
set(sctp_debug OFF CACHE BOOL "" FORCE)
set(sctp_inet ON CACHE BOOL "" FORCE)
set(sctp_inet6 ON CACHE BOOL "" FORCE)

FetchContent_Declare(
  usrsctp
  GIT_REPOSITORY https://github.com/sctplab/usrsctp.git
  GIT_TAG 0.9.5.0
)

FetchContent_MakeAvailable(usrsctp)
