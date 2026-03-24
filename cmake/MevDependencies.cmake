include_guard(GLOBAL)

set(MEV_TOMLPLUSPLUS_TAG "v3.4.0")
set(MEV_WHISPER_TAG "v1.7.4")
set(MEV_LIBFVAD_TAG "master")

function(mev_require_fetchcontent dep_name)
  if(NOT MEV_FETCH_DEPS)
    message(FATAL_ERROR
      "${dep_name} is required but downloads are disabled (MEV_FETCH_DEPS=OFF).\n"
      "Provide the dependency locally and re-run configure.\n"
      "Supported local overrides:\n"
      "  -DMEV_TOMLPLUSPLUS_SOURCE_DIR=/abs/path/to/tomlplusplus\n"
      "  -DMEV_WHISPER_SOURCE_DIR=/abs/path/to/whisper.cpp\n"
      "  -DMEV_LIBFVAD_SOURCE_DIR=/abs/path/to/libfvad\n")
  endif()
endfunction()

function(mev_add_local_dependency dep_name dep_source_dir)
  if(NOT EXISTS "${dep_source_dir}/CMakeLists.txt")
    message(FATAL_ERROR
      "${dep_name} source override does not look like a CMake project:\n"
      "  ${dep_source_dir}")
  endif()

  string(TOLOWER "${dep_name}" dep_slug)
  add_subdirectory(
    "${dep_source_dir}"
    "${CMAKE_BINARY_DIR}/_deps/${dep_slug}-local"
    EXCLUDE_FROM_ALL)
endfunction()

function(mev_enable_tomlplusplus)
  if(TARGET tomlplusplus::tomlplusplus)
    return()
  endif()

  if(DEFINED MEV_TOMLPLUSPLUS_SOURCE_DIR AND NOT "${MEV_TOMLPLUSPLUS_SOURCE_DIR}" STREQUAL "")
    mev_add_local_dependency("toml++" "${MEV_TOMLPLUSPLUS_SOURCE_DIR}")
    return()
  endif()

  find_package(tomlplusplus CONFIG QUIET)
  if(TARGET tomlplusplus::tomlplusplus)
    return()
  endif()

  mev_require_fetchcontent("toml++")

  include(FetchContent)
  FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        ${MEV_TOMLPLUSPLUS_TAG}
  )
  FetchContent_MakeAvailable(tomlplusplus)
endfunction()

function(mev_enable_whispercpp)
  if(TARGET whisper)
    return()
  endif()

  if(DEFINED MEV_WHISPER_SOURCE_DIR AND NOT "${MEV_WHISPER_SOURCE_DIR}" STREQUAL "")
    set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    mev_add_local_dependency("whisper.cpp" "${MEV_WHISPER_SOURCE_DIR}")
    return()
  endif()

  mev_require_fetchcontent("whisper.cpp")

  include(FetchContent)
  FetchContent_Declare(
    whisper
    GIT_REPOSITORY https://github.com/ggerganov/whisper.cpp.git
    GIT_TAG        ${MEV_WHISPER_TAG}
  )
  set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(whisper)
endfunction()

function(mev_enable_libfvad)
  if(TARGET fvad)
    return()
  endif()

  if(DEFINED MEV_LIBFVAD_SOURCE_DIR AND NOT "${MEV_LIBFVAD_SOURCE_DIR}" STREQUAL "")
    mev_add_local_dependency("libfvad" "${MEV_LIBFVAD_SOURCE_DIR}")
    return()
  endif()

  mev_require_fetchcontent("libfvad")

  include(FetchContent)
  FetchContent_Declare(
    libfvad
    GIT_REPOSITORY https://github.com/dpirch/libfvad.git
    GIT_TAG        ${MEV_LIBFVAD_TAG}
  )
  FetchContent_MakeAvailable(libfvad)
endfunction()
