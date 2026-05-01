add_library(cubey STATIC
    app_config.cpp
    vulkan/device.cpp
    vulkan/instance.cpp
)
add_library(cubey::cubey ALIAS cubey)

target_include_directories(
    cubey
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
        "${vulkan_headers_SOURCE_DIR}/include"
)

target_link_libraries(
    cubey
    PUBLIC
        cubey_project_options
        PkgConfig::VULKAN_LOADER
    PRIVATE
        cubey_project_warnings
)
