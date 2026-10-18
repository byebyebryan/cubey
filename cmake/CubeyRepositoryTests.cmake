include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/CubeyFoundationChecks.cmake")

# Derive application coverage from the configured targets, including shared
# implementation files outside a frontend's source directory. Disabled studies
# never register a target and therefore cannot enter this scan.
function(cubey_add_render_app_policy_test)
    get_property(app_targets GLOBAL PROPERTY CUBEY_APP_TARGETS)
    if (NOT app_targets)
        return()
    endif()

    set(scan_dirs)
    foreach(target IN LISTS app_targets)
        get_target_property(source_root "${target}" SOURCE_DIR)
        get_target_property(sources "${target}" SOURCES)
        foreach(source IN LISTS sources)
            get_filename_component(absolute_source "${source}" ABSOLUTE BASE_DIR "${source_root}")
            get_filename_component(source_dir "${absolute_source}" DIRECTORY)
            list(APPEND scan_dirs "${source_dir}")
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES scan_dirs)
    list(SORT scan_dirs)

    set(classic_rendering_pattern
        "vkCreateRenderPass|vkDestroyRenderPass|VkRenderPass|vkCreateFramebuffer|vkDestroyFramebuffer|VkFramebuffer|vkCmdBeginRenderPass|vkCmdEndRenderPass|VkRenderPassBeginInfo"
    )
    add_test(
        NAME render_apps_use_dynamic_rendering
        COMMAND /bin/sh -c
            "pattern=\"$1\"; shift; grep -RE \"$pattern\" \"$@\"; status=$?; test \"$status\" -eq 1"
            render_apps_use_dynamic_rendering
            "${classic_rendering_pattern}"
            ${scan_dirs}
    )
    set_tests_properties(render_apps_use_dynamic_rendering PROPERTIES LABELS "policy")
endfunction()

# Runtime paths come from configured app targets, never guessed build folders.
function(cubey_write_documented_app_manifest output)
    get_property(app_targets GLOBAL PROPERTY CUBEY_APP_TARGETS)
    set(entries)
    foreach(target IN LISTS app_targets)
        get_target_property(source "${target}" SOURCE_DIR)
        file(RELATIVE_PATH source "${CMAKE_SOURCE_DIR}" "${source}")
        string(APPEND entries "    {\"source\": \"${source}\", \"binary\": \"$<TARGET_FILE:${target}>\"},\n")
    endforeach()
    string(REGEX REPLACE ",\n$" "\n" entries "${entries}")
    file(GENERATE OUTPUT "${output}" CONTENT "{\n  \"applications\": [\n${entries}  ]\n}\n")
endfunction()

function(cubey_add_repository_tests)
    cubey_add_render_app_policy_test()
    cubey_add_public_header_checks()
    cubey_write_foundation_target_graph("${CMAKE_BINARY_DIR}/foundation-targets.json")

    file(GLOB_RECURSE capture_scripts CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/projects/capture*.sh"
        "${CMAKE_SOURCE_DIR}/projects/*/capture*.sh"
        "${CMAKE_SOURCE_DIR}/projects/*/tools/capture*.sh"
        "${CMAKE_SOURCE_DIR}/studies/cloud/terrainengine/tools/capture*.sh"
        "${CMAKE_SOURCE_DIR}/studies/planet/tools/capture*.sh"
        "${CMAKE_SOURCE_DIR}/studies/terrain/landscapes/capture*.sh"
    )
    if (capture_scripts)
        add_test(
            NAME capture_review_scripts_use_headless
            COMMAND /bin/sh -c
                "failed=0; for script in \"$@\"; do if ! grep -q -- '--headless' \"$script\"; then printf 'capture review script does not select --headless: %s\n' \"$script\"; failed=1; fi; done; exit \"$failed\""
                capture_review_scripts_use_headless
                ${capture_scripts}
        )
        set_tests_properties(capture_review_scripts_use_headless PROPERTIES LABELS "headless;policy")
    endif()

    set(CUBEY_REPOSITORY_PYTHON "${Python3_EXECUTABLE}")
    if (CUBEY_REPOSITORY_PYTHON)
        add_test(NAME foundation_layers
            COMMAND "${CUBEY_REPOSITORY_PYTHON}" -B "${CMAKE_SOURCE_DIR}/tools/check_foundation_layers.py"
                --target-graph "${CMAKE_BINARY_DIR}/foundation-targets.json")
        add_test(NAME foundation_layer_rules
            COMMAND "${CUBEY_REPOSITORY_PYTHON}" -B "${CMAKE_SOURCE_DIR}/tests/tools/test_foundation_layers.py"
                --cmake "${CMAKE_COMMAND}")
        set_tests_properties(foundation_layers foundation_layer_rules PROPERTIES LABELS "unit;build;policy" TIMEOUT 30)
        add_test(NAME project_layout_hygiene
            COMMAND "${CUBEY_REPOSITORY_PYTHON}" -B "${CMAKE_SOURCE_DIR}/tools/check_project_layout.py")
        add_test(NAME project_layout_rules
            COMMAND "${CUBEY_REPOSITORY_PYTHON}" -B "${CMAKE_SOURCE_DIR}/tests/tools/test_project_layout.py")
        add_test(NAME documentation_hygiene
            COMMAND "${CUBEY_REPOSITORY_PYTHON}" -B "${CMAKE_SOURCE_DIR}/tools/check_documentation.py")
        add_test(NAME documentation_media
            COMMAND "${CUBEY_REPOSITORY_PYTHON}" -B "${CMAKE_SOURCE_DIR}/tools/check_documentation.py" --check media)
        add_test(NAME documentation_rules
            COMMAND "${CUBEY_REPOSITORY_PYTHON}" -B "${CMAKE_SOURCE_DIR}/tests/tools/test_documentation.py")
        set_tests_properties(documentation_hygiene documentation_media documentation_rules
            PROPERTIES LABELS "unit;docs;policy" TIMEOUT 30)
        cubey_write_documented_app_manifest("${CMAKE_BINARY_DIR}/documented-apps.json")
        get_property(documented_apps GLOBAL PROPERTY CUBEY_APP_TARGETS)
        if (documented_apps)
            add_test(NAME documentation_cli_recipes
                COMMAND "${CUBEY_REPOSITORY_PYTHON}" -B "${CMAKE_SOURCE_DIR}/tools/check_documentation.py"
                    --check recipes --app-manifest "${CMAKE_BINARY_DIR}/documented-apps.json")
            set_tests_properties(documentation_cli_recipes PROPERTIES
                LABELS "unit;docs;config" TIMEOUT 60
                ENVIRONMENT "VK_DRIVER_FILES=/nonexistent/cubey-docs-no-icd.json;VK_ICD_FILENAMES=/nonexistent/cubey-docs-no-icd.json;DISPLAY=;WAYLAND_DISPLAY=")
        endif()
        add_test(NAME cmake_app_policy_rules
            COMMAND "${CUBEY_REPOSITORY_PYTHON}" -B "${CMAKE_SOURCE_DIR}/tests/tools/test_cmake_app_policy.py"
                --cmake "${CMAKE_COMMAND}" --ctest "${CMAKE_CTEST_COMMAND}")
        add_test(NAME shader_build_rules
            COMMAND "${CUBEY_REPOSITORY_PYTHON}" -B "${CMAKE_SOURCE_DIR}/tests/tools/test_shader_build.py"
                --cmake "${CMAKE_COMMAND}")
        set_tests_properties(shader_build_rules PROPERTIES LABELS "unit;build;policy" TIMEOUT 60)
        add_test(NAME report_server_tests
            COMMAND "${CUBEY_REPOSITORY_PYTHON}" -B "${CMAKE_SOURCE_DIR}/tests/tools/test_report_server.py")
        set_tests_properties(project_layout_hygiene project_layout_rules cmake_app_policy_rules
            PROPERTIES LABELS "unit;docs;policy" TIMEOUT 30)
        set_tests_properties(report_server_tests PROPERTIES LABELS "unit;tools" TIMEOUT 30)
    endif()
endfunction()
