function(mev_enable_sanitizers target)
  if(NOT MEV_ENABLE_SANITIZERS)
    return()
  endif()

  if(MSVC)
    message(WARNING "Sanitizers are not configured for MSVC in this project")
    return()
  endif()

  target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(${target} PRIVATE -fsanitize=address,undefined)
endfunction()
