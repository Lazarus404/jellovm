function(jellovm_enable_sanitizers target_name)
  if(MSVC)
    return()
  endif()

  if(JELLOVM_ENABLE_ASAN)
    # Use PUBLIC so dependents also link the sanitizer runtime.
    target_compile_options(${target_name} PUBLIC -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(${target_name} PUBLIC -fsanitize=address)
  endif()

  if(JELLOVM_ENABLE_UBSAN)
    # Use PUBLIC so dependents also link the sanitizer runtime.
    target_compile_options(${target_name} PUBLIC -fsanitize=undefined -fno-omit-frame-pointer)
    target_link_options(${target_name} PUBLIC -fsanitize=undefined)
  endif()
endfunction()

