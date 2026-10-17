# Included from the Fluid frontend directory; source/binary paths stay frontend-owned.
find_program(CUBEY_FLUID_RECORDING_PYTHON NAMES python3)
if (CUBEY_FLUID_RECORDING_PYTHON)
    add_test(NAME fluid_25d_presentation_helpers
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -B -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_present_fluid25d_v1.py)
    set_tests_properties(fluid_25d_presentation_helpers PROPERTIES LABELS "unit;fluid;presentation" TIMEOUT 30)
    add_test(NAME fluid_25d_presentation_v2_helpers
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -B -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_present_fluid25d_v2.py)
    set_tests_properties(fluid_25d_presentation_v2_helpers PROPERTIES LABELS "unit;fluid;presentation" TIMEOUT 30)
    add_test(NAME fluid_25d_presentation_docs
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -B -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_presentation_docs_v1.py)
    set_tests_properties(fluid_25d_presentation_docs PROPERTIES LABELS "unit;fluid;presentation" TIMEOUT 30)
    add_test(NAME fluid_25d_dependency_setup_contract
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -B -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/projects/terrain_water/external_synxflow" -p test_setup_contract.py)
    set_tests_properties(fluid_25d_dependency_setup_contract PROPERTIES LABELS "unit;fluid;backend" TIMEOUT 30)
    add_test(NAME fluid_25d_presentation_media
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -B -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_presentation_media_v1.py)
    set_tests_properties(fluid_25d_presentation_media PROPERTIES LABELS "unit;fluid;presentation" TIMEOUT 30)
    add_test(NAME fluid_25d_rain_response_helpers
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -B -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_run_rain_response_v1.py)
    set_tests_properties(fluid_25d_rain_response_helpers PROPERTIES LABELS "unit;fluid;presentation" TIMEOUT 30)
    add_test(NAME fluid_25d_rain_response_review_helpers
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -B -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_review_rain_response_v1.py)
    set_tests_properties(fluid_25d_rain_response_review_helpers PROPERTIES LABELS "unit;fluid;presentation" TIMEOUT 30)
    add_test(NAME fluid_25d_captured_water_alignment_report
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -B -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_report_captured_water_alignment_v1.py)
    set_tests_properties(fluid_25d_captured_water_alignment_report PROPERTIES LABELS "unit;fluid;terrain" TIMEOUT 30)
    add_test(NAME fluid_25d_terrain_presentation_review_helpers
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -B -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_review_terrain_presentation_v1.py)
    set_tests_properties(fluid_25d_terrain_presentation_review_helpers PROPERTIES LABELS "unit;fluid;presentation" TIMEOUT 30)
    add_test(NAME fluid_25d_offscreen_performance_helpers
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -B -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_run_offscreen_perf_v2.py)
    set_tests_properties(fluid_25d_offscreen_performance_helpers PROPERTIES LABELS "unit;fluid;presentation" TIMEOUT 30)
    add_test(NAME fluid_25d_terrain_demo_launcher
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -B -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_run_terrain_rain_demo.py)
    set_tests_properties(fluid_25d_terrain_demo_launcher PROPERTIES LABELS "unit;fluid;presentation" TIMEOUT 30)
    add_test(NAME fluid_25d_patch_only_review_helpers
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -B "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness/test_review_patch_only_v1.py")
    set_tests_properties(fluid_25d_patch_only_review_helpers PROPERTIES LABELS "unit;fluid;presentation" TIMEOUT 30)
    add_test(NAME fluid_25d_environment_review_helpers
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -B "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness/test_review_environment_v1.py")
    set_tests_properties(fluid_25d_environment_review_helpers PROPERTIES LABELS "unit;fluid;presentation" TIMEOUT 30)
    add_test(NAME fluid_25d_terrain_site_catalogue
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -B -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_terrain_water_sites.py)
    set_tests_properties(fluid_25d_terrain_site_catalogue PROPERTIES LABELS "unit;fluid;terrain" TIMEOUT 30)
    add_test(NAME fluid_25d_world_water_study_helpers
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -B -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_study_world_water_v1.py)
    set_tests_properties(fluid_25d_world_water_study_helpers PROPERTIES LABELS "unit;fluid;terrain" TIMEOUT 30)
    add_test(NAME fluid_25d_live_performance_review
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -B -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_review_live_perf_terrain_v1.py)
    set_tests_properties(fluid_25d_live_performance_review PROPERTIES LABELS "unit;fluid;presentation" TIMEOUT 30)
    add_test(NAME fluid_25d_live_performance_analysis
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -B -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_analyze_live_perf_terrain_v1.py)
    set_tests_properties(fluid_25d_live_performance_analysis PROPERTIES LABELS "unit;fluid;presentation" TIMEOUT 30)
    add_test(NAME fluid_25d_live_performance_orchestration
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -B -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_finish_live_perf_terrain_v1.py)
    set_tests_properties(fluid_25d_live_performance_orchestration PROPERTIES LABELS "unit;fluid;presentation" TIMEOUT 30)
    add_test(NAME fluid_25d_water_rendering_reuse_helpers
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -B -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_review_water_rendering_reuse_v1.py)
    add_test(NAME fluid_25d_external_worker_host
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/projects/terrain_water/external_synxflow" -p test_session_worker.py)
    add_test(NAME fluid_25d_external_worker_build_helpers
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/projects/terrain_water/external_synxflow" -p test_build_synxflow_service_v1.py)
    set_tests_properties(fluid_25d_external_worker_host fluid_25d_external_worker_build_helpers
        PROPERTIES LABELS "unit;fluid;backend" TIMEOUT 30)
    add_test(NAME fluid_25d_source_build_pin
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_build_synxflow_source_v1.py)
    set_tests_properties(fluid_25d_source_build_pin PROPERTIES LABELS "unit;fluid;dependency" TIMEOUT 30)
    set(CUBEY_FLUID_RECORDING_VIDEO_SMOKE_ARGS)
    if (CUBEY_HAS_LIBAV)
        list(APPEND CUBEY_FLUID_RECORDING_VIDEO_SMOKE_ARGS --video)
    endif()
    add_test(NAME fluid_25d_recording_converter
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_convert_synxflow_recording_v1.py)
    add_test(NAME fluid_25d_live_session_publisher
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_live_synxflow_session_v1.py)
    set_tests_properties(fluid_25d_live_session_publisher PROPERTIES LABELS "unit;fluid;stream" TIMEOUT 60)
    add_test(NAME fluid_25d_live_view_acceptance_helpers
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_live_view_acceptance_v1.py)
    set_tests_properties(fluid_25d_live_view_acceptance_helpers PROPERTIES LABELS "unit;fluid;stream" TIMEOUT 60)
    add_test(NAME fluid_25d_native_presentation_helpers
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_run_native_presentation_v1.py)
    set_tests_properties(fluid_25d_native_presentation_helpers
        PROPERTIES LABELS "unit;fluid;presentation" TIMEOUT 60)
    add_test(NAME fluid_25d_mountain_demo_launcher
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_run_mountain_rain_demo.py)
    set_tests_properties(fluid_25d_mountain_demo_launcher
        PROPERTIES LABELS "unit;fluid;presentation" TIMEOUT 60)
    add_test(NAME fluid_25d_terrain_demo_supervisor
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_terrain_demo_session.py)
    set_tests_properties(fluid_25d_terrain_demo_supervisor PROPERTIES LABELS "unit;fluid;backend" TIMEOUT 60)
    add_test(NAME fluid_25d_terrain_demo_evidence_helpers
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_validate_terrain_demo_v1.py)
    set_tests_properties(fluid_25d_terrain_demo_evidence_helpers PROPERTIES LABELS "unit;fluid;presentation" TIMEOUT 30)
    add_test(NAME fluid_25d_mountain_demo_review_helpers
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_review_mountain_rain_demo_v1.py)
    set_tests_properties(fluid_25d_mountain_demo_review_helpers
        PROPERTIES LABELS "unit;fluid;presentation" TIMEOUT 60)
    add_test(NAME fluid_25d_scenic_review_helpers
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_review_scenic_water_v1.py)
    set_tests_properties(fluid_25d_scenic_review_helpers
        PROPERTIES LABELS "unit;fluid;presentation" TIMEOUT 60)
    add_test(NAME fluid_25d_scenic_material_review_helpers
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_review_scenic_material_v2.py)
    set_tests_properties(fluid_25d_scenic_material_review_helpers
        PROPERTIES LABELS "unit;fluid;presentation" TIMEOUT 60)
    add_test(NAME fluid_25d_terrain_review_helpers
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_review_terrain_material_v3.py)
    set_tests_properties(fluid_25d_terrain_review_helpers
        PROPERTIES LABELS "unit;fluid;presentation" TIMEOUT 60)
    add_test(NAME fluid_25d_macro_review_helpers
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_macro_terrain_v4.py)
    set_tests_properties(fluid_25d_macro_review_helpers
        PROPERTIES LABELS "unit;fluid;presentation" TIMEOUT 60)
    add_test(NAME fluid_25d_scenic_water_helpers
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_scenic_water.py)
    set_tests_properties(fluid_25d_scenic_water_helpers
        PROPERTIES LABELS "unit;fluid;presentation" TIMEOUT 60)
    add_test(NAME fluid_25d_steep_flow_review_helpers
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_review_steep_flow_v1.py)
    set_tests_properties(fluid_25d_steep_flow_review_helpers
        PROPERTIES LABELS "unit;fluid;presentation" TIMEOUT 60)
    add_test(NAME fluid_25d_whitewater_review_helpers
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_review_whitewater_v1.py)
    set_tests_properties(fluid_25d_whitewater_review_helpers
        PROPERTIES LABELS "unit;fluid;presentation" TIMEOUT 60)
    add_test(NAME fluid_25d_stream_foam_review_helpers
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_review_stream_foam_v1.py)
    set_tests_properties(fluid_25d_stream_foam_review_helpers
        PROPERTIES LABELS "unit;fluid;presentation" TIMEOUT 60)
    add_test(NAME fluid_25d_bank_edge_review_helpers
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_review_bank_edge_v1.py)
    set_tests_properties(fluid_25d_bank_edge_review_helpers
        PROPERTIES LABELS "unit;fluid;presentation" TIMEOUT 60)
    add_test(NAME fluid_25d_bank_edge_gpu
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}"
            "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness/test_bank_edge_gpu.py"
            --app "$<TARGET_FILE:cubey_project_fluid_25d>")
    set_tests_properties(fluid_25d_bank_edge_gpu
        PROPERTIES LABELS "gpu;fluid;presentation" TIMEOUT 300)
    add_test(NAME fluid_25d_whitewater_gpu
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}"
            "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness/test_whitewater_gpu.py"
            --app "$<TARGET_FILE:cubey_project_fluid_25d>")
    set_tests_properties(fluid_25d_whitewater_gpu
        PROPERTIES LABELS "gpu;fluid;presentation" TIMEOUT 600)
    add_test(NAME fluid_25d_steep_flow_gpu
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}"
            "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness/test_steep_flow_gpu.py"
            --app "$<TARGET_FILE:cubey_project_fluid_25d>")
    set_tests_properties(fluid_25d_steep_flow_gpu
        PROPERTIES LABELS "gpu;fluid;presentation" TIMEOUT 300)
    add_test(NAME fluid_25d_cascade_cues_gpu
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}"
            "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness/test_steep_flow_gpu.py"
            --app "$<TARGET_FILE:cubey_project_fluid_25d>" --variant cascade)
    set_tests_properties(fluid_25d_cascade_cues_gpu
        PROPERTIES LABELS "gpu;fluid;presentation" TIMEOUT 300)
    add_test(NAME fluid_25d_rain_visuals_gpu
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}"
            "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness/test_rain_visuals_gpu.py"
            --target $<TARGET_FILE:cubey_project_fluid_25d>
            ${CUBEY_FLUID_RECORDING_VIDEO_SMOKE_ARGS})
    set_tests_properties(fluid_25d_rain_visuals_gpu
        PROPERTIES LABELS "gpu;fluid;rain;presentation" TIMEOUT 240)
    add_test(NAME fluid_25d_scenic_water_gpu
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}"
            "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness/test_scenic_water_gpu.py"
            --target "$<TARGET_FILE:cubey_project_fluid_25d>")
    set_tests_properties(fluid_25d_scenic_water_gpu
        PROPERTIES LABELS "headless;gpu;fluid;presentation" TIMEOUT 600)
    add_test(NAME fluid_25d_shallow_coverage_gpu
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}"
            "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness/test_scenic_water_gpu.py"
            --target "$<TARGET_FILE:cubey_project_fluid_25d>" --coverage-only)
    set_tests_properties(fluid_25d_shallow_coverage_gpu
        PROPERTIES LABELS "headless;gpu;fluid;presentation" TIMEOUT 240)
    add_test(NAME fluid_25d_water_agitation_gpu
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}"
            "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness/test_water_agitation_gpu.py"
            --target "$<TARGET_FILE:cubey_project_fluid_25d>")
    set_tests_properties(fluid_25d_water_agitation_gpu
        PROPERTIES LABELS "headless;gpu;fluid;presentation" TIMEOUT 360)
    add_test(NAME fluid_25d_native_bank_helpers
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_run_native_bank_render_v1.py)
    set_tests_properties(fluid_25d_native_bank_helpers
        PROPERTIES LABELS "unit;fluid;presentation" TIMEOUT 60)
    add_test(NAME fluid_25d_native_bank_reconstruction_helpers
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_run_native_bank_reconstruction_v2.py)
    set_tests_properties(fluid_25d_native_bank_reconstruction_helpers
        PROPERTIES LABELS "unit;fluid;presentation" TIMEOUT 60)
    add_test(NAME fluid_25d_native_shoreline_raster_helpers
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_native_shoreline_raster_v1.py)
    add_test(NAME fluid_25d_native_bank_presentation_helpers
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_run_native_bank_presentation_v4.py)
    set_tests_properties(fluid_25d_native_shoreline_raster_helpers fluid_25d_native_bank_presentation_helpers
        PROPERTIES LABELS "unit;fluid;presentation" TIMEOUT 60)
    add_test(NAME fluid_25d_native_bank_refinement_helpers
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}" -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness" -p test_native_bank_refinement_v1.py)
    set_tests_properties(fluid_25d_native_bank_refinement_helpers
        PROPERTIES LABELS "unit;fluid;presentation" TIMEOUT 60)
    add_test(NAME fluid_25d_native_shoreline_raster_gpu
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}"
            "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness/run_native_shoreline_raster_v1.py"
            --target "$<TARGET_FILE:cubey_project_fluid_25d>")
    set_tests_properties(fluid_25d_native_shoreline_raster_gpu
        PROPERTIES LABELS "headless;gpu;fluid;presentation" SKIP_RETURN_CODE 77 TIMEOUT 180)
    add_test(NAME fluid_25d_display_coverage_gpu
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}"
            "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness/test_display_coverage_gpu_v1.py"
            --target "$<TARGET_FILE:cubey_project_fluid_25d>"
            ${CUBEY_FLUID_RECORDING_VIDEO_SMOKE_ARGS})
    set_tests_properties(fluid_25d_display_coverage_gpu
        PROPERTIES LABELS "headless;gpu;fluid;presentation" SKIP_RETURN_CODE 77 TIMEOUT 180)
    add_test(NAME fluid_25d_bank_comparison_gpu
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}"
            "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness/test_bank_comparison_gpu_v1.py"
            --target "$<TARGET_FILE:cubey_project_fluid_25d>"
            ${CUBEY_FLUID_RECORDING_VIDEO_SMOKE_ARGS})
    set_tests_properties(fluid_25d_bank_comparison_gpu
        PROPERTIES LABELS "headless;gpu;fluid;presentation" SKIP_RETURN_CODE 77 TIMEOUT 240)
    add_test(NAME fluid_25d_recording_gpu_immutability
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}"
            "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness/test_recording_viewer_gpu_v1.py"
            --target "$<TARGET_FILE:cubey_project_fluid_25d>"
            ${CUBEY_FLUID_RECORDING_VIDEO_SMOKE_ARGS})
    set_tests_properties(fluid_25d_recording_gpu_immutability
        PROPERTIES LABELS "headless;gpu;recording" SKIP_RETURN_CODE 77 TIMEOUT 150)
    add_test(NAME fluid_25d_scenic_gpu
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}"
            "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness/test_scenic_gpu_v1.py"
            --target "$<TARGET_FILE:cubey_project_fluid_25d>"
            ${CUBEY_FLUID_RECORDING_VIDEO_SMOKE_ARGS})
    set_tests_properties(fluid_25d_scenic_gpu
        # Independent application startups now include V3 and macro controls.
        PROPERTIES LABELS "headless;gpu;fluid;presentation" SKIP_RETURN_CODE 77 TIMEOUT 600)
    add_test(NAME fluid_25d_backend_startup_gpu
        COMMAND "${CUBEY_FLUID_RECORDING_PYTHON}"
            "${CMAKE_SOURCE_DIR}/studies/fluid_25d/harness/test_backend_startup_gpu_v1.py"
            --target "$<TARGET_FILE:cubey_project_fluid_25d>")
    set_tests_properties(fluid_25d_backend_startup_gpu
        PROPERTIES LABELS "headless;gpu;backend" SKIP_RETURN_CODE 77 TIMEOUT 180)
endif()
