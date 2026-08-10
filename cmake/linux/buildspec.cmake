# CMake Linux OBS compatibility dependency module

include_guard(GLOBAL)

include(buildspec_common)

function(_check_dependencies_linux)
  set(arch x86_64)
  set(platform linux-x86_64)

  set(dependencies_dir
    "${CMAKE_CURRENT_SOURCE_DIR}/.deps/${OBS_COMPATIBILITY_TARGET}"
  )
  set(obs-studio_filename "VERSION.tar.gz")
  set(obs-studio_destination "obs-studio-VERSION")
  set(dependencies_list obs-studio)

  _check_dependencies()
endfunction()

_check_dependencies_linux()
