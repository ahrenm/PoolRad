# The default build uses a fixed upstream source archive and verifies its bytes.
# External packages/SDKs are an explicit opt-out from the pinned dependency.
option(POOLRAD_FETCH_SDL3 "Fetch and build the pinned SDL3 source release" ON)
set(POOLRAD_SDL3_ROOT "" CACHE PATH "Windows SDL3 SDK root (include, lib, bin)")
set(POOLRAD_SDL3_LICENSE "${CMAKE_CURRENT_LIST_DIR}/../third_party/SDL3-LICENSE.txt")
if(POOLRAD_FETCH_SDL3)
    if(POOLRAD_SDL3_ROOT)
        message(FATAL_ERROR "Set POOLRAD_FETCH_SDL3=OFF to use POOLRAD_SDL3_ROOT")
    endif()
    include(FetchContent)
    # Official release-3.4.20 asset digest, not a moving branch or unverified tag.
    set(SDL_SHARED ON CACHE BOOL "Build shared SDL3 for PoolRad" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "Do not build an unused static SDL3" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "Do not build SDL's test library" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "Do not build SDL's tests" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "Do not build SDL's examples" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "PoolRad owns runtime-only installation" FORCE)
    set(SDL_UNINSTALL OFF CACHE BOOL "Do not add SDL uninstall targets" FORCE)
    FetchContent_Declare(SDL3
        URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.20/SDL3-3.4.20.tar.gz
        URL_HASH SHA256=f3bb06f55bed9557227a700af2ea62e49497ebdf58037732a731a5fb3ef8581f
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
        TLS_VERIFY TRUE
        SYSTEM)
    FetchContent_MakeAvailable(SDL3)
    set(POOLRAD_SDL3_LICENSE "${sdl3_SOURCE_DIR}/LICENSE.txt")
elseif(POOLRAD_SDL3_ROOT)
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
