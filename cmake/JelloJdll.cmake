# Helpers for building Jello Dynamic Extension Libraries (`.jdll`).

include_guard(GLOBAL)

set(_JELLO_JDLL_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

# jello_jdll_embed_abi(<abi_file> <out_var>)
#
# Registers a build rule that converts `<abi_file>` into a generated C source
# embedding the ABI blob. Sets `<out_var>` to the generated `.c` path.
function(jello_jdll_embed_abi abi_file out_var)
  if(NOT abi_file OR NOT out_var)
    message(FATAL_ERROR "jello_jdll_embed_abi requires ABI_FILE and OUT_VAR")
  endif()

  get_filename_component(_abi_abs "${abi_file}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  if(NOT EXISTS "${_abi_abs}")
    message(FATAL_ERROR "JDLL ABI file not found: ${_abi_abs}")
  endif()

  get_filename_component(_abi_name "${_abi_abs}" NAME_WE)
  set(_out_c "${CMAKE_CURRENT_BINARY_DIR}/${_abi_name}_abi_embed.c")

  add_custom_command(
    OUTPUT "${_out_c}"
    COMMAND ${CMAKE_COMMAND}
            -DABI_FILE=${_abi_abs}
            -DOUT_C=${_out_c}
            -P ${_JELLO_JDLL_CMAKE_DIR}/EmbedJdllAbi.cmake
    DEPENDS "${_abi_abs}"
    COMMENT "Embed JDLL ABI from ${_abi_name}.abi"
    VERBATIM
  )

  set("${out_var}" "${_out_c}" PARENT_SCOPE)
endfunction()
