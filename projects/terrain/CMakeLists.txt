add_executable(cubey_project_terrain
    main.cpp
    terrain_generator.cpp
    terrain_product.cpp
)
set_target_properties(cubey_project_terrain PROPERTIES OUTPUT_NAME terrain)
target_include_directories(cubey_project_terrain PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(
    cubey_project_terrain
    PRIVATE
        cubey::core
        cubey_project_warnings
)

if (BUILD_TESTING)
    add_executable(cubey_project_terrain_tests
        terrain_generator.cpp
        terrain_product.cpp
        terrain_tests.cpp
    )
    target_include_directories(cubey_project_terrain_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(
        cubey_project_terrain_tests
        PRIVATE
            cubey::core
            cubey_project_warnings
    )
    add_test(NAME terrain_product_tests COMMAND cubey_project_terrain_tests)
endif()
