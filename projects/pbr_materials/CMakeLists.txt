cubey_forward_pbr_shader_sources(CUBEY_FORWARD_PBR_SHADERS)
cubey_forward_pbr_shader_depends(CUBEY_FORWARD_PBR_SHADER_DEPENDS)
cubey_cloud_layer_shader_sources(CUBEY_WORKSHOP_CLOUD_SHADERS COMPOSITE background-depth)
cubey_cloud_layer_shader_depends(CUBEY_WORKSHOP_CLOUD_DEPENDS)

cubey_add_app_target(
    cubey_project_pbr_materials
    OUTPUT_NAME pbr_materials
    SHADER_DEFINE CUBEY_PBR_MATERIALS_SHADER_DIR
    WINDOWED_SMOKE_TEST pbr_materials_reaches_glfw
    WINDOWED_SMOKE_PATTERN "pbr_materials: .*rendering PBR material workshop"
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
    add_test(NAME pbr_materials_view_replay
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tools/test_view_replay.py"
            --binary "$<TARGET_FILE:cubey_project_pbr_materials>"
            --output "${CMAKE_CURRENT_BINARY_DIR}/view-replay")
    set_tests_properties(pbr_materials_view_replay PROPERTIES
        TIMEOUT 120 SKIP_RETURN_CODE 77 LABELS "headless;gpu;artifact"
        ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/cmake/lsan.supp")
    cubey_add_png_capture_test(pbr_materials_environment_transmission_depth_reuse
        cubey_project_pbr_materials "${CMAKE_CURRENT_BINARY_DIR}/environment-depth-reuse.png"
        128 96 --require-validation --frames 4 --material gallery --lighting clear
        --transmission-source environment)
    cubey_add_png_capture_test(pbr_materials_transmission_hdr_readback
        cubey_project_pbr_materials "${CMAKE_CURRENT_BINARY_DIR}/transmission-hdr-readback.png"
        128 96 --require-validation --frames 1 --material glass --lighting clear
        --debug-view transmission-ray
        --hdr-output "${CMAKE_CURRENT_BINARY_DIR}/transmission-hdr-readback.f32")
    set_tests_properties(pbr_materials_environment_transmission_depth_reuse
        pbr_materials_transmission_hdr_readback PROPERTIES
        ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1"
        FAIL_REGULAR_EXPRESSION "vulkan validation error")
    foreach(material plain gallery)
        cubey_add_png_capture_test(pbr_materials_depth_reuse_${material} cubey_project_pbr_materials
            "${CMAKE_CURRENT_BINARY_DIR}/depth-reuse-${material}.png" 128 96
            --require-validation --frames 4 --material ${material} --lighting clear)
        set_property(TEST pbr_materials_depth_reuse_${material} APPEND PROPERTY
            ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1")
    endforeach()
    add_executable(cubey_material_recipe_tests material_recipe_tests.cpp)
    target_link_libraries(cubey_material_recipe_tests PRIVATE cubey::cubey cubey_project_options cubey_project_warnings)
    target_compile_definitions(cubey_material_recipe_tests PRIVATE CUBEY_MATERIAL_RECIPE_DIR="${PROJECT_SOURCE_DIR}/assets/materials")
    add_test(NAME material_recipe_tests COMMAND cubey_material_recipe_tests)
    add_test(NAME material_library_report_tests
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tools/library_report_tests.py")
endif()

if(BUILD_TESTING)
    add_executable(cubey_workshop_config_tests workshop_config_tests.cpp ../sdf/sdf_timeline.cpp ../sdf/sdf_recipe.cpp ../sdf/sdf_render_modes.cpp)
    target_link_libraries(cubey_workshop_config_tests PRIVATE cubey::host cubey::cubey cubey_project_options cubey_project_warnings)
    add_test(NAME workshop_config_tests COMMAND cubey_workshop_config_tests)
endif()
