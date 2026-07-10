set(required_files
    manifest.json
    source_height_m.png
    mountain_support.png
    height_m.png
    slope.png
    curvature.png
    local_relief_m.png
)

foreach(file IN LISTS required_files)
    set(path "${OUTPUT_DIR}/${file}")
    if (NOT EXISTS "${path}")
        message(FATAL_ERROR "terrain export is missing ${path}")
    endif()
    file(SIZE "${path}" size)
    if (size LESS 8)
        message(FATAL_ERROR "terrain export is empty: ${path}")
    endif()
endforeach()
