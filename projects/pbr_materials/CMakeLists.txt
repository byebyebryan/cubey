cubey_forward_pbr_shader_sources(CUBEY_FORWARD_PBR_SHADERS)
cubey_forward_pbr_shader_depends(CUBEY_FORWARD_PBR_SHADER_DEPENDS)
cubey_cloud_layer_shader_sources(CUBEY_WORKSHOP_CLOUD_SHADERS COMPOSITE background-depth)
cubey_cloud_layer_shader_depends(CUBEY_WORKSHOP_CLOUD_DEPENDS)

cubey_add_app_target(
    cubey_project_pbr_materials
    OUTPUT_NAME pbr_materials
    SHADER_DEFINE CUBEY_PBR_MATERIALS_SHADER_DIR
    WINDOWED_SMOKE_TEST pbr_materials_reaches_glfw
    WINDOWED_SMOKE_PATTERN "pbr_materials: .*rendering material instance cubes"
    SOURCES
        main.cpp
        pbr_materials_app.cpp
        pbr_materials_render.cpp
        pbr_materials_resources.cpp
        pbr_materials_scene.cpp
        pbr_materials_workshop.cpp
        pbr_materials_environment.cpp
    SHADERS
        ${CUBEY_FORWARD_PBR_SHADERS}
        ${CUBEY_WORKSHOP_CLOUD_SHADERS}
    SHADER_INCLUDE_DIRS
        "${CMAKE_SOURCE_DIR}/shaders"
    SHADER_DEPENDS
        ${CUBEY_FORWARD_PBR_SHADER_DEPENDS}
        ${CUBEY_WORKSHOP_CLOUD_DEPENDS}
)

target_compile_definitions(cubey_project_pbr_materials PRIVATE CUBEY_MATERIAL_RECIPE_DIR="${PROJECT_SOURCE_DIR}/assets/materials")

if(BUILD_TESTING)
    add_executable(cubey_material_recipe_tests material_recipe_tests.cpp)
    target_link_libraries(cubey_material_recipe_tests PRIVATE cubey::cubey cubey_project_options cubey_project_warnings)
    target_compile_definitions(cubey_material_recipe_tests PRIVATE CUBEY_MATERIAL_RECIPE_DIR="${PROJECT_SOURCE_DIR}/assets/materials")
    add_test(NAME material_recipe_tests COMMAND cubey_material_recipe_tests)
endif()
