cmake_minimum_required(VERSION 3.16)

# Allow building `vm/` standalone or via top-level `./CMakeLists.txt`.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_LIST_DIR)
  project(jellovm
    VERSION 0.1.0
    LANGUAGES C
  )
endif()

# --- Basics ------------------------------------------------------------------
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(JELLOVM_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(JELLOVM_ENABLE_ASAN "Enable AddressSanitizer (clang/gcc)" OFF)
option(JELLOVM_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer (clang/gcc)" OFF)
# LTO and computed goto default ON for Release builds (override with -D to disable).
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(_jellovm_default_lto ON)
  set(_jellovm_default_cgoto ON)
else()
  set(_jellovm_default_lto OFF)
  set(_jellovm_default_cgoto OFF)
endif()
option(JELLOVM_ENABLE_LTO "Enable link-time optimization if supported" ${_jellovm_default_lto})
option(JELLOVM_BUILD_TESTS "Build tests" ON)
option(JELLOVM_USE_COMPUTED_GOTO "Use computed goto for opcode dispatch (GCC/Clang; ~10-20% faster)" ${_jellovm_default_cgoto})
option(JELLOVM_REFERENCE_INTERP "Use reference interpreter path (no hot-op inlining)" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

include(CompilerWarnings)
include(Sanitizers)

# Put all binaries in one place (nice for local dev).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# --- Targets -----------------------------------------------------------------
add_library(jellovm
  src/vm.c
  src/gc.c
  src/bytecode/loader.c
  src/bytecode/check.c
  src/vm/interp.c
  src/vm/reg.c
  src/vm/spill.c
  src/vm/box.c
  src/vm/call.c
  src/vm/frame.c
  src/vm/exc.c
  src/vm/ops/dispatch.c
  src/vm/ops/exec_loop.c
  src/vm/ops/ops_control.c
  src/vm/ops/ops_const.c
  src/vm/ops/ops_call.c
  src/vm/ops/ops_builtins.c
  src/vm/ops/ops_arith.c
  src/vm/ops/ops_box.c
  src/vm/ops/ops_list.c
  src/vm/ops/ops_array.c
  src/vm/ops/ops_bytes.c
  src/vm/ops/ops_obj.c
  src/types/bytes.c
  src/types/box.c
  src/types/function.c
  src/types/abstract.c
  src/types/object.c
  src/types/array.c
  src/types/list.c
  src/types/strutil.c
)

target_include_directories(jellovm
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/src/include>"
    "$<INSTALL_INTERFACE:include>"
)

target_compile_definitions(jellovm
  PUBLIC
    JELLOVM_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    JELLOVM_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    JELLOVM_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

if(JELLOVM_USE_COMPUTED_GOTO)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_definitions(jellovm PRIVATE JELLOVM_USE_COMPUTED_GOTO=1)
  else()
    message(WARNING "JELLOVM_USE_COMPUTED_GOTO requires GCC or Clang; ignoring")
  endif()
endif()

if(JELLOVM_REFERENCE_INTERP)
  target_compile_definitions(jellovm PRIVATE JELLOVM_REFERENCE_INTERP=1)
endif()

jellovm_enable_warnings(jellovm)
jellovm_enable_sanitizers(jellovm)

if(JELLOVM_ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_err)
  if(_ipo_ok)
    set_property(TARGET jellovm PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  else()
    message(WARNING "IPO/LTO not supported: ${_ipo_err}")
  endif()
endif()

add_executable(jellovm_cli
  src/main.c
)
set_target_properties(jellovm_cli PROPERTIES OUTPUT_NAME "jellovm")
target_link_libraries(jellovm_cli PRIVATE jellovm)
jellovm_enable_warnings(jellovm_cli)
jellovm_enable_sanitizers(jellovm_cli)

install(FILES src/include/jello.h DESTINATION include)

# --- Tests -------------------------------------------------------------------
if(JELLOVM_BUILD_TESTS)
  include(CTest)
  if(BUILD_TESTING)
    # Tests live at repo-root `./ctest` by convention.
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../ctest" "${CMAKE_BINARY_DIR}/ctest")
  endif()
endif()
