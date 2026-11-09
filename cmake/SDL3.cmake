# Prefer SDL's exported package. A compiler-matched Windows SDK without CMake
# metadata can instead be selected explicitly with POOLRAD_SDL3_ROOT.
set(POOLRAD_SDL3_ROOT "" CACHE PATH "Windows SDL3 SDK root (include, lib, bin)")
if(POOLRAD_SDL3_ROOT)
    if(NOT WIN32)
        message(FATAL_ERROR "POOLRAD_SDL3_ROOT is the Windows SDK fallback; use SDL3_DIR or CMAKE_PREFIX_PATH on other hosts")
    endif()
    # Changing the explicit SDK must not silently reuse a previous SDK's cache.
    unset(poolrad_sdl_include CACHE)
    unset(poolrad_sdl_import CACHE)
    unset(poolrad_sdl_runtime CACHE)
    find_path(poolrad_sdl_include SDL3/SDL.h PATHS "${POOLRAD_SDL3_ROOT}/include" NO_DEFAULT_PATH REQUIRED)
    if(MSVC)
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(sdl_arch x64)
        else()
            set(sdl_arch x86)
        endif()
        find_library(poolrad_sdl_import NAMES SDL3 PATHS "${POOLRAD_SDL3_ROOT}/lib/${sdl_arch}" "${POOLRAD_SDL3_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
    else()
        find_library(poolrad_sdl_import NAMES SDL3 PATHS "${POOLRAD_SDL3_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
    endif()
    find_file(poolrad_sdl_runtime SDL3.dll PATHS "${POOLRAD_SDL3_ROOT}/bin" "${POOLRAD_SDL3_ROOT}/lib/${sdl_arch}" "${POOLRAD_SDL3_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
    add_library(SDL3::SDL3-shared SHARED IMPORTED)
    set_target_properties(SDL3::SDL3-shared PROPERTIES
        IMPORTED_IMPLIB "${poolrad_sdl_import}"
        IMPORTED_LOCATION "${poolrad_sdl_runtime}"
        INTERFACE_INCLUDE_DIRECTORIES "${poolrad_sdl_include}")
else()
    find_package(SDL3 3.2 REQUIRED CONFIG COMPONENTS SDL3-shared)
endif()
