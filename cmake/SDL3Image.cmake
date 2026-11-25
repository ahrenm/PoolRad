include(FetchContent)

# Link the JPEG decoder into PoolRad without adding another runtime library.
block(SCOPE_FOR VARIABLES PROPAGATE POOLRAD_SDL3_IMAGE_LICENSE)
    set(BUILD_SHARED_LIBS OFF)
    set(SDLIMAGE_INSTALL OFF)
    set(SDLIMAGE_SAMPLES OFF)
    set(SDLIMAGE_TESTS OFF)
    set(SDLIMAGE_VENDORED OFF)
    set(SDLIMAGE_BACKEND_STB ON)
    set(SDLIMAGE_BACKEND_WIC OFF)
    set(SDLIMAGE_BACKEND_IMAGEIO OFF)
    set(SDLIMAGE_JPG ON)
    set(SDLIMAGE_JPG_SAVE OFF)
    foreach(format ANI AVIF BMP GIF JXL LBM PCX PNG PNM QOI SVG TGA TIF WEBP XCF XPM XV)
        set(SDLIMAGE_${format} OFF)
    endforeach()

    FetchContent_Declare(SDL3_image
        URL https://github.com/libsdl-org/SDL_image/releases/download/release-3.4.8/SDL3_image-3.4.8.tar.gz
        URL_HASH SHA256=e8223b424bc7541cf84ffff5cf7e4f24ec3711c60462cb1bd6dc1d7bd7f25477
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
        TLS_VERIFY TRUE
        SYSTEM)
    FetchContent_MakeAvailable(SDL3_image)
    set(POOLRAD_SDL3_IMAGE_LICENSE "${sdl3_image_SOURCE_DIR}/LICENSE.txt")
endblock()
