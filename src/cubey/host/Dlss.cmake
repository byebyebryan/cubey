set(CUBEY_DLSS_SDK_DIR "" CACHE PATH "Developer-provided NVIDIA DLSS v310.9.16 SDK root (Linux x86-64)")
add_library(cubey_dlss STATIC dlss_upscaler.cpp)
add_library(cubey::dlss ALIAS cubey_dlss)
target_link_libraries(cubey_dlss PUBLIC cubey::render PRIVATE cubey_project_warnings)

if(CUBEY_DLSS_SDK_DIR)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
        message(FATAL_ERROR "Cubey's DLSS integration requires Linux x86-64")
    endif()

    # NVIDIA/DLSS v310.9.16, commit 9981c6350dc58c0ce21f024fd65e31e851c31689.
    # Read the developer's original SDK in place. No vendor artifacts are downloaded or packaged.
    get_filename_component(dlss_sdk "${CUBEY_DLSS_SDK_DIR}" ABSOLUTE)
    set(dlss_lib "${dlss_sdk}/lib/Linux_x86_64/libnvsdk_ngx.a")
    set(dlss_feature_dir "${dlss_sdk}/lib/Linux_x86_64/rel")
    set(dlss_feature "${dlss_feature_dir}/libnvidia-ngx-dlss.so.310.9.16")
    set(dlss_headers
        nvsdk_ngx.h nvsdk_ngx_defs.h nvsdk_ngx_defs_vk.h nvsdk_ngx_helpers.h
        nvsdk_ngx_helpers_cuda.h nvsdk_ngx_helpers_d3d.h nvsdk_ngx_helpers_vk.h
        nvsdk_ngx_params.h nvsdk_ngx_vk.h
    )
    set(dlss_header_digests "")
    foreach(header IN LISTS dlss_headers)
        file(SHA256 "${dlss_sdk}/include/${header}" digest)
        string(APPEND dlss_header_digests "${digest}")
    endforeach()
    string(SHA256 header_hash "${dlss_header_digests}")
    file(SHA256 "${dlss_lib}" lib_hash)
    file(SHA256 "${dlss_feature}" feature_hash)
    if(NOT header_hash STREQUAL "375256d75c55349bd46adc4454b68977734be3ce5fd865a8588c1271017f6edc"
       OR NOT lib_hash STREQUAL "d3c50bb0d9624fc35e502a8e929b8fc1e2b505017ebdb84c0d5b627e11a235df"
       OR NOT feature_hash STREQUAL "30982c937afed85c96b8d313323de9a650a2eec6de1bb0f714b21750f4ce98f6")
        message(FATAL_ERROR "CUBEY_DLSS_SDK_DIR must contain the unmodified NVIDIA/DLSS v310.9.16 Linux x86-64 SDK")
    endif()
    target_include_directories(cubey_dlss SYSTEM PRIVATE "${dlss_sdk}/include")
    target_compile_definitions(cubey_dlss PRIVATE CUBEY_DLSS_FEATURE_DIR="${dlss_feature_dir}")
    target_link_libraries(cubey_dlss PRIVATE "${dlss_lib}" ${CMAKE_DL_LIBS})
endif()
