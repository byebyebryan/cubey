include_guard(GLOBAL)

# Compile each configured library's public headers in isolation, with that
# target's real public usage requirements. Keep this out of ordinary builds;
# CTest and explicit foundation reviews invoke the aggregate target.
function(cubey_add_public_header_checks)
    file(GLOB_RECURSE headers CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/include/cubey/*.h")
    set(modules)
    foreach(header IN LISTS headers)
        file(RELATIVE_PATH relative "${PROJECT_SOURCE_DIR}/include" "${header}")
        string(REGEX REPLACE "^cubey/([^/]+)/.*" "\\1" module "${relative}")
        if (module STREQUAL "procedural")
            set(module core)
        endif()
        if (NOT TARGET cubey::${module})
            continue() # Optional host headers follow the configured host target.
        endif()
        set(unit "${CMAKE_CURRENT_BINARY_DIR}/header_checks/${relative}.cpp")
        file(GENERATE OUTPUT "${unit}" CONTENT "#include <${relative}>\n")
        list(APPEND units_${module} "${unit}")
        list(APPEND modules "${module}")
    endforeach()
    list(REMOVE_DUPLICATES modules)
    add_custom_target(cubey_header_checks)
    foreach(module IN LISTS modules)
        add_library(cubey_${module}_header_checks OBJECT EXCLUDE_FROM_ALL ${units_${module}})
        target_link_libraries(cubey_${module}_header_checks PRIVATE cubey::${module} cubey_project_warnings)
        add_dependencies(cubey_header_checks cubey_${module}_header_checks)
    endforeach()
    add_test(NAME foundation_public_headers
        COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --target cubey_header_checks --parallel 2)
    set_tests_properties(foundation_public_headers PROPERTIES LABELS "build;policy" TIMEOUT 300)
endfunction()
