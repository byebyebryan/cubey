if(NOT DEFINED APP OR NOT DEFINED OUTPUT OR NOT DEFINED PROFILE)
    message(FATAL_ERROR "terrain ShaderToy profile validation requires APP, OUTPUT, and PROFILE")
endif()

file(
    REMOVE
    "${OUTPUT}"
    "${PROFILE}.frames.csv"
    "${PROFILE}.passes.csv"
    "${PROFILE}.metrics.csv"
    "${PROFILE}.trace.json"
    "${PROFILE}.summary.txt"
)
execute_process(
    COMMAND
        "${APP}"
        --headless
        --capture video
        --frames 8
        --fps 8
        --width 320
        --height 180
        --output "${OUTPUT}"
        --profile-output "${PROFILE}"
        --profile-warmup-frames 1
        --reference-render mesh
        --reference-time 20
        --reference-mesh-cells 256
        --reference-normal atlas
        --reference-shading clay
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "terrain ShaderToy profile failed: ${stdout}${stderr}")
endif()

file(READ "${PROFILE}.passes.csv" passes)
string(FIND "${passes}" "gpu,terrain_shadertoy_ref.sky" sky_index)
string(FIND "${passes}" "gpu,terrain_shadertoy_ref.surface" surface_index)
if(sky_index EQUAL -1 OR surface_index EQUAL -1)
    message(FATAL_ERROR "terrain ShaderToy profile did not record sky and surface GPU spans")
endif()
