# Headless previews of the round status screen: tools/preview.sh builds this
# and writes one PNG per screen state to docs/previews/.
cmake_minimum_required(VERSION 3.24)
project(grocery_preview LANGUAGES C)

include(FetchContent)
set(CMAKE_C_STANDARD 11)

set(REPO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(COMPONENT_DIR "${REPO_DIR}/components/grocery_button")

# LVGL 9.5.0, the version the SDK pins for the device.
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE FILEPATH "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    lvgl
    URL "https://github.com/lvgl/lvgl/archive/refs/tags/v9.5.0.tar.gz"
    URL_HASH "SHA256=34a955cdf3a2d005507b704e87357af669a114523b6d3f77b5344fdc68717bc6"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(lvgl)
find_package(ZLIB REQUIRED)

add_executable(grocery_preview
    preview.c
    "${COMPONENT_DIR}/grocery_ui.c"
    "${COMPONENT_DIR}/grocery_parse.c"
)
target_include_directories(grocery_preview PRIVATE "${COMPONENT_DIR}/include")
target_link_libraries(grocery_preview PRIVATE lvgl::lvgl ZLIB::ZLIB m)
