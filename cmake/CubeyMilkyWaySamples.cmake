set(CUBEY_MILKY_WAY_SAMPLE_ASSETS_FETCH_DIR
    "${CMAKE_BINARY_DIR}/_deps/cubey_milky_way_sample_assets-src"
)
set(CUBEY_MILKY_WAY_PANORAMA_URL
    "https://sos.noaa.gov/ftp_mirror/astronomy/milky_way/all_sky/2048.jpg"
)
set(CUBEY_MILKY_WAY_PANORAMA_SHA256
    "7ec4ac3afc42c48651f937f8e89bbc6354386867e8d5bc7a745e12fb5a8480c1"
)
set(CUBEY_NASA_DEEP_STAR_MAP_8K_URL
    "https://svs.gsfc.nasa.gov/vis/a000000/a003800/a003895/starmap_8k.jpg"
)
set(CUBEY_NASA_DEEP_STAR_MAP_8K_SHA256
    "ec28e645863d55d4c0513a07fc846eaf06fc4f4b2246e4a6b10535f990309360"
)

function(cubey_download_milky_way_sample filename url sha256)
    set(output "${CUBEY_MILKY_WAY_SAMPLE_ASSETS_FETCH_DIR}/${filename}")
    set(needs_download TRUE)

    if (EXISTS "${output}")
        file(SHA256 "${output}" existing_sha256)
        if (existing_sha256 STREQUAL "${sha256}")
            set(needs_download FALSE)
        else()
            file(REMOVE "${output}")
        endif()
    endif()

    if (needs_download)
        file(
            DOWNLOAD
                "${url}"
                "${output}"
            EXPECTED_HASH "SHA256=${sha256}"
            TLS_VERIFY ON
            STATUS download_status
            LOG download_log
        )
        list(GET download_status 0 download_result)
        if (NOT download_result EQUAL 0)
            list(GET download_status 1 download_message)
            message(
                FATAL_ERROR
                "Failed to fetch Milky Way sample asset ${filename}: ${download_message}\n${download_log}"
            )
        endif()
    endif()
endfunction()

function(cubey_fetch_milky_way_sample_assets)
    file(MAKE_DIRECTORY "${CUBEY_MILKY_WAY_SAMPLE_ASSETS_FETCH_DIR}")

    cubey_download_milky_way_sample(
        "starmap_8k.jpg"
        "${CUBEY_NASA_DEEP_STAR_MAP_8K_URL}"
        "${CUBEY_NASA_DEEP_STAR_MAP_8K_SHA256}"
    )
    cubey_download_milky_way_sample(
        "2048.jpg"
        "${CUBEY_MILKY_WAY_PANORAMA_URL}"
        "${CUBEY_MILKY_WAY_PANORAMA_SHA256}"
    )

    set(
        CUBEY_MILKY_WAY_ASSETS_DIR
        "${CUBEY_MILKY_WAY_SAMPLE_ASSETS_FETCH_DIR}"
        CACHE PATH
        "Existing Cubey Milky Way sample asset directory"
        FORCE
    )
endfunction()
