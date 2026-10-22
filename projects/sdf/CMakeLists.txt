cubey_add_app_target(
    cubey_project_sdf
    OUTPUT_NAME sdf
    SHADER_DEFINE CUBEY_SDF_SHADER_DIR
    WINDOWED_SMOKE_TEST sdf_reaches_glfw
    WINDOWED_SMOKE_PATTERN "sdf: .*rendering sparse SDF bricks"
    SOURCES main.cpp sdf_app.cpp sdf_field.cpp sdf_recipe.cpp sdf_validation.cpp sdf_renderer.cpp sdf_timeline.cpp sdf_authored.cpp sdf_history.cpp
    SHADERS shaders/sdf.vert shaders/sdf.frag shaders/sdf_authored.frag shaders/filter.comp shaders/bricks.comp shaders/validate.comp
        shaders/authored_filter.comp shaders/authored_bricks.comp
        shaders/authored_validate.comp
        shaders/occluder.vert shaders/occluder.frag
        "${CMAKE_SOURCE_DIR}/shaders/cubey/forward_pbr/forward_pbr_post.vert"
        "${CMAKE_SOURCE_DIR}/shaders/cubey/forward_pbr/forward_pbr_post.frag"
    SHADER_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    SHADER_DEPENDS shaders/field.glsl shaders/trace.glsl
)

if (BUILD_TESTING)
    add_executable(cubey_project_sdf_recipe_tests sdf_recipe.cpp sdf_recipe_tests.cpp sdf_timeline.cpp)
    target_link_libraries(cubey_project_sdf_recipe_tests PRIVATE cubey::cubey cubey_project_options cubey_project_warnings)
    add_test(NAME sdf_recipe_tests COMMAND cubey_project_sdf_recipe_tests)
    add_executable(cubey_project_sdf_authored_tests sdf_authored.cpp sdf_history.cpp sdf_timeline.cpp sdf_recipe.cpp sdf_authored_tests.cpp)
    target_link_libraries(cubey_project_sdf_authored_tests PRIVATE cubey::cubey cubey_project_options cubey_project_warnings)
    add_test(NAME sdf_authored_tests COMMAND cubey_project_sdf_authored_tests)
    add_test(NAME sdf_normal_projection_tests
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/normal_projection_tests.py")
    add_test(NAME sdf_mirror_author_tests
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/mirror_author_tests.py")
endif()
