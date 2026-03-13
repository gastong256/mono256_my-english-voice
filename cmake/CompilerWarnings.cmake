function(mev_set_warnings target)
  if(NOT MEV_ENABLE_STRICT_WARNINGS)
    return()
  endif()

  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /WX)
  else()
    target_compile_options(
      ${target}
      PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Woverloaded-virtual
        -Wformat=2
        -Wimplicit-fallthrough
        -Werror
    )
  endif()
endfunction()
