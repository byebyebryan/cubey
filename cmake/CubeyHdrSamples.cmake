set(CUBEY_FILAMENT_ENVIRONMENT_COMMIT
    "b82d42f9f0889907787a2eef17e61e77f2055cbb"
)
set(CUBEY_HDR_SAMPLE_ASSETS_FETCH_DIR
    "${CMAKE_BINARY_DIR}/_deps/cubey_hdr_sample_assets-src"
)
set(CUBEY_FILAMENT_ENVIRONMENT_BASE_URL
    "https://raw.githubusercontent.com/google/filament/${CUBEY_FILAMENT_ENVIRONMENT_COMMIT}/third_party/environments"
)

function(cubey_download_hdr_sample filename sha256)
    set(output "${CUBEY_HDR_SAMPLE_ASSETS_FETCH_DIR}/${filename}")
    set(needs_download TRUE)

    if (EXISTS "${output}")
        file(SHA256 "${output}" existing_sha256)
        if (existing_sha256 STREQUAL sha256)
            set(needs_download FALSE)
        else()
            file(REMOVE "${output}")
        endif()
    endif()

    if (needs_download)
        file(
            DOWNLOAD
                "${CUBEY_FILAMENT_ENVIRONMENT_BASE_URL}/${filename}"
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
                "Failed to fetch HDR sample ${filename}: ${download_message}\n${download_log}"
            )
        endif()
    endif()
endfunction()

function(cubey_fetch_hdr_sample_assets)
    file(MAKE_DIRECTORY "${CUBEY_HDR_SAMPLE_ASSETS_FETCH_DIR}")

    cubey_download_hdr_sample(
        "lightroom_14b.hdr"
        "adccd934033f8a6f71ee25208c5cc082ee5f8989c4ad66ea001d795760952b15"
    )
    cubey_download_hdr_sample(
        "flower_road_2k.hdr"
        "aa36ad9b4a9832097798de2a59a9009069c99f6cca1538722bd65e5c4af38c27"
    )
    cubey_download_hdr_sample(
        "venetian_crossroads_2k.hdr"
        "6f8289439f47bc7b641ec94d00e01774560d41e45724cde9c86c2fd5cb8ecb4d"
    )
    cubey_download_hdr_sample(
        "CC0.html"
        "a7a419d9b0345b8addf97b49a45c093fa402d3e4e15f8472de5d84d6f1b617af"
    )
    cubey_download_hdr_sample(
        "URL.txt"
        "174db00639b4cb876a7de65dafef4a85e2795e6b7e1157a450b71e256ef456a6"
    )

    set(
        CUBEY_HDR_SAMPLE_ASSETS_DIR
        "${CUBEY_HDR_SAMPLE_ASSETS_FETCH_DIR}"
        CACHE PATH
        "Existing Cubey HDR sample asset directory"
        FORCE
    )
endfunction()
