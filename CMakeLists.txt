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
option(JELLOVM_ENABLE_TSAN "Enable ThreadSanitizer (clang/gcc; use for thread ctests)" OFF)
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
option(JELLOVM_ENABLE_JIT "Enable partial JIT (experimental)" ON)
option(JELLOVM_JIT_ARM64 "Enable ARM64 JIT backend when host is aarch64" ON)
option(JELLOVM_JIT_X64 "Enable x86-64 JIT backend when host is x86_64" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

include(CompilerWarnings)
include(JelloJdll)
include(Sanitizers)

# Put all binaries in one place (nice for local dev).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

if(MINGW)
  option(JELLOVM_SHARED "Build jellovm as a DLL (required for .jdll plugins on MinGW)" ON)
endif()
option(JELLOVM_BUILD_STD "Build std.jdll reference plugin" ON)

if(JELLOVM_BUILD_STD)
  # Reference JDLL plugins (std.jdll, …). Must be registered before ctest when tests are enabled.
  add_subdirectory(libs/std)
endif()

# --- Targets -----------------------------------------------------------------
set(_jellovm_lib_type STATIC)
if(MINGW AND JELLOVM_SHARED)
  # PE/COFF .jdll plugins need jdl_* at link time; resolve via jellovm.dll import lib.
  set(_jellovm_lib_type SHARED)
endif()

add_library(jellovm ${_jellovm_lib_type}
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
  src/vm/math.c
  src/vm/ops/ops_arith.c
  src/vm/ops/ops_box.c
  src/vm/ops/ops_list.c
  src/vm/ops/ops_array.c
  src/vm/ops/ops_bytes.c
  src/vm/ops/ops_obj.c
  src/vm/ops/ops_enum.c
  src/types/bytes.c
  src/types/box.c
  src/types/function.c
  src/types/abstract.c
  src/types/object.c
  src/types/array.c
  src/types/list.c
  src/types/enum.c
  src/types/strutil.c
  src/thread/thread.c
  src/thread/value_copy.c
  src/thread/channel.c
  src/ffi/jdll_loader.c
  src/ffi/jdll_deps.c
  src/ffi/jdll_marshal.c
  src/ffi/jdll_callback.c
  src/ffi/jdll_trace.c
  src/boot/boot.c
  src/jit/jit_config.c
)

if(JELLOVM_ENABLE_JIT)
  set(_jellovm_jit_host_x64 FALSE)
  if(CMAKE_OSX_ARCHITECTURES)
    if("x86_64" IN_LIST CMAKE_OSX_ARCHITECTURES)
      set(_jellovm_jit_host_x64 TRUE)
    endif()
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    set(_jellovm_jit_host_x64 TRUE)
  endif()
  if(JELLOVM_JIT_ARM64 AND NOT (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64|arm64") AND NOT (CMAKE_OSX_ARCHITECTURES AND "arm64" IN_LIST CMAKE_OSX_ARCHITECTURES))
    message(STATUS "JELLOVM_JIT_ARM64 disabled: host is not ARM64")
    set(JELLOVM_JIT_ARM64 OFF)
  endif()
  if(JELLOVM_JIT_X64 AND NOT _jellovm_jit_host_x64)
    message(STATUS "JELLOVM_JIT_X64 disabled: host is not x86_64")
    set(JELLOVM_JIT_X64 OFF)
  endif()
  set(_jellovm_jit_sources
    src/jit/jit_compile.c
    src/jit/jit_cache.c
    src/jit/jit_ir.c
    src/jit/jit_deopt.c
    src/jit/jit_runtime.c
    src/jit/backend/jit_emit_buf.c
  )
  if(JELLOVM_JIT_ARM64)
    list(APPEND _jellovm_jit_sources src/jit/backend/arm64/jit_arm64_emit.c)
  endif()
  if(JELLOVM_JIT_X64)
    list(APPEND _jellovm_jit_sources src/jit/backend/x64/jit_x64_emit.c)
  endif()
  if(JELLOVM_JIT_ARM64 OR JELLOVM_JIT_X64)
    target_sources(jellovm PRIVATE ${_jellovm_jit_sources})
    target_compile_definitions(jellovm PRIVATE JELLOVM_ENABLE_JIT=1)
    if(JELLOVM_JIT_ARM64)
      target_compile_definitions(jellovm PRIVATE JELLOVM_JIT_ARM64=1)
    endif()
    if(JELLOVM_JIT_X64)
      target_compile_definitions(jellovm PRIVATE JELLOVM_JIT_X64=1)
    endif()
  else()
    message(STATUS "JIT enabled in options but no host backend available; using interpreter only")
    target_sources(jellovm PRIVATE src/jit/jit_disabled.c)
  endif()
else()
  target_sources(jellovm PRIVATE src/jit/jit_disabled.c)
endif()

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

if(UNIX AND NOT APPLE)
  target_link_libraries(jellovm PUBLIC dl pthread m)
  target_compile_definitions(jellovm PRIVATE _GNU_SOURCE)
elseif(APPLE)
  target_link_libraries(jellovm PUBLIC dl pthread m)
elseif(MINGW)
  target_link_libraries(jellovm PUBLIC pthread m dl)
  if(JELLOVM_SHARED)
    target_link_options(jellovm PRIVATE "-Wl,--export-all-symbols")
  endif()
endif()

if(JELLOVM_ENABLE_LTO)
  if(MINGW AND JELLOVM_SHARED)
    message(STATUS "JELLOVM_ENABLE_LTO disabled: MinGW shared jellovm.dll import libs are unreliable with LTO")
  else()
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_err)
    if(_ipo_ok)
      set_property(TARGET jellovm PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
      message(WARNING "IPO/LTO not supported: ${_ipo_err}")
    endif()
  endif()
endif()

add_executable(jellovm_cli
  src/main.c
)
set_target_properties(jellovm_cli PROPERTIES OUTPUT_NAME "jellovm")
target_link_libraries(jellovm_cli PRIVATE jellovm)
jellovm_enable_warnings(jellovm_cli)
jellovm_enable_sanitizers(jellovm_cli)
if(APPLE)
  # Export VM symbols so .jdll plugins can resolve jdl_* at dlopen time.
  target_link_options(jellovm_cli PRIVATE "-Wl,-export_dynamic")
elseif(UNIX)
  target_link_options(jellovm_cli PRIVATE "-rdynamic")
elseif(MINGW)
  # With JELLOVM_SHARED, jdl_* live in libjellovm.dll; do not regenerate its import lib from the exe.
  if(NOT JELLOVM_SHARED)
    target_link_options(jellovm_cli PRIVATE "-Wl,--export-all-symbols")
  endif()
endif()

install(TARGETS jellovm jellovm_cli
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)
install(FILES src/include/jello.h DESTINATION include)
install(FILES src/include/jello/jdll.h DESTINATION include/jello)

# --- Tests -------------------------------------------------------------------
if(JELLOVM_BUILD_TESTS)
  include(CTest)
  if(BUILD_TESTING)
    # Tests live in the sibling `jello-compiler` repo.
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../jello-compiler/ctest" "${CMAKE_BINARY_DIR}/ctest")
  endif()
endif()
