add_library(cubey_app STATIC
    glfw_window.cpp
    windowed_host.cpp
)
add_library(cubey::app ALIAS cubey_app)

target_include_directories(
    cubey_app
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
)

target_compile_definitions(
    cubey_app
    PRIVATE
        GLFW_INCLUDE_NONE
)

target_link_libraries(
    cubey_app
    PUBLIC
        cubey::cubey
    PRIVATE
        cubey_project_warnings
        glfw
)
