# Tests — unit tests, golden image tests, benchmarks

include(FetchContent)

# Google Test
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Google Benchmark (optional)
option(SPECTRA_BUILD_BENCHMARKS "Build performance benchmarks" OFF)
if(SPECTRA_BUILD_BENCHMARKS)
    FetchContent_Declare(
        googlebenchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        v1.8.3
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googlebenchmark)
endif()

# Helper function
function(add_spectra_test subdir name)
    string(REPLACE "/" "_" target_name "${subdir}_${name}")
    add_executable(${target_name} ${subdir}/${name}.cpp)
    target_link_libraries(${target_name} PRIVATE spectra GTest::gtest_main)
    target_compile_features(${target_name} PRIVATE cxx_std_20)
    # Tests need access to internal src/ headers (e.g. core/transform.hpp)
    # and shared test utilities (tests/util/)
    target_include_directories(${target_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/util
    )
    # Propagate feature flags to tests for conditional compilation
    target_compile_definitions(${target_name} PRIVATE
        $<$<BOOL:${SPECTRA_USE_FFMPEG}>:SPECTRA_USE_FFMPEG>
    )
    add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

# Unit tests — guarded by if(EXISTS) so parallel agents don't break each other's builds.
# When adding a new test, just append the name to this list.
set(SPECTRA_UNIT_TESTS
    test_transform
    test_layout
    test_timeline
    test_easing
    test_ring_buffer
    test_input
    test_series_data
    test_command_queue
    test_decimation
    test_filters
    test_tick_generation
    test_series_visibility
    test_ui_icons
    test_svg_export
    test_resize_layout
    test_layout_manager
    test_inspector
    test_animation_controller
    test_gesture_recognizer
    test_data_interaction
    test_transition_engine
    test_theme
    test_command_registry
    test_undo_manager
    test_command_palette_registry
    test_shortcut_manager
    test_undo_redo
    test_workspace
    test_region_select
    test_legend_interaction
    test_figure_manager
    test_inspector_stats
    test_box_zoom_overlay
    test_undo_property
    test_workspace_v2
    test_timeline_editor
    test_recording_export
    test_theme_colorblind
    test_phase2_integration
    test_split_view
    test_dock_system
    test_plot_style
    test_axis_link
    test_data_transform
    test_shared_cursor
    test_keyframe_interpolator
    test_camera_animator
    test_shortcut_config
    test_plugin_api
    test_workspace_v3
    test_phase3_integration
    test_math3d
    test_3d_pipelines
    test_camera
    # test_axes3d  # Not a proper GTEST file, uses plain functions with assert
    test_series3d
    test_depth_buffer
    test_3d_integration
    test_transparency
    test_mode_transition
    test_workspace_3d
    test_3d_regression
    test_multi_window
    test_window_manager
    test_figure_registry
    test_ipc
    test_session_graph
    test_process_manager
    test_easy_api
)

# Tests that require a GPU/display (Vulkan + GLFW window)
set(SPECTRA_GPU_TESTS
    test_3d_pipelines
    test_depth_buffer
    test_3d_integration
    test_transparency
    test_mode_transition
    test_workspace_3d
    test_3d_regression
    test_multi_window
    test_window_manager
    test_figure_registry
    test_phase2_integration
    test_phase3_integration
)

foreach(_test ${SPECTRA_UNIT_TESTS})
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/${_test}.cpp)
        add_spectra_test(unit ${_test})
        # Tag GPU-dependent tests so they can be excluded with -LE gpu
        if(_test IN_LIST SPECTRA_GPU_TESTS)
            set_tests_properties(unit_${_test} PROPERTIES LABELS "gpu")
        endif()
    endif()
endforeach()

# Golden image tests (require Vulkan — headless via lavapipe in CI)
option(SPECTRA_BUILD_GOLDEN_TESTS "Build golden image regression tests" ON)
if(SPECTRA_BUILD_GOLDEN_TESTS)
    add_executable(golden_image_tests golden/golden_test.cpp)
    target_link_libraries(golden_image_tests PRIVATE spectra GTest::gtest_main)
    target_compile_features(golden_image_tests PRIVATE cxx_std_20)
    target_include_directories(golden_image_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/golden
    )
    add_test(NAME golden_image_tests COMMAND golden_image_tests)

    # Label golden tests so CI can run them separately
    set_tests_properties(golden_image_tests PROPERTIES LABELS "golden;gpu")

    # Phase 2 golden image tests
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/golden/golden_test_phase2.cpp)
        add_executable(golden_image_tests_phase2 golden/golden_test_phase2.cpp)
        target_link_libraries(golden_image_tests_phase2 PRIVATE spectra GTest::gtest_main)
        target_compile_features(golden_image_tests_phase2 PRIVATE cxx_std_20)
        target_include_directories(golden_image_tests_phase2 PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/golden
        )
        add_test(NAME golden_image_tests_phase2 COMMAND golden_image_tests_phase2)
        set_tests_properties(golden_image_tests_phase2 PROPERTIES LABELS "golden;gpu")
    endif()

    # Phase 3 golden image tests
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/golden/golden_test_phase3.cpp)
        add_executable(golden_image_tests_phase3 golden/golden_test_phase3.cpp)
        target_link_libraries(golden_image_tests_phase3 PRIVATE spectra GTest::gtest_main)
        target_compile_features(golden_image_tests_phase3 PRIVATE cxx_std_20)
        target_include_directories(golden_image_tests_phase3 PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/golden
        )
        add_test(NAME golden_image_tests_phase3 COMMAND golden_image_tests_phase3)
        set_tests_properties(golden_image_tests_phase3 PROPERTIES LABELS "golden;gpu")
    endif()

    # 3D Phase 3 golden image tests
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/golden/golden_test_3d_phase3.cpp)
        add_executable(golden_image_tests_3d_phase3 golden/golden_test_3d_phase3.cpp)
        target_link_libraries(golden_image_tests_3d_phase3 PRIVATE spectra GTest::gtest_main)
        target_compile_features(golden_image_tests_3d_phase3 PRIVATE cxx_std_20)
        target_include_directories(golden_image_tests_3d_phase3 PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/golden
        )
        add_test(NAME golden_image_tests_3d_phase3 COMMAND golden_image_tests_3d_phase3)
        set_tests_properties(golden_image_tests_3d_phase3 PROPERTIES LABELS "golden;gpu")
    endif()

    # 3D golden image tests
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/golden/golden_test_3d.cpp)
        add_executable(golden_image_tests_3d golden/golden_test_3d.cpp)
        target_link_libraries(golden_image_tests_3d PRIVATE spectra GTest::gtest_main)
        target_compile_features(golden_image_tests_3d PRIVATE cxx_std_20)
        target_include_directories(golden_image_tests_3d PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/golden
        )
        add_test(NAME golden_image_tests_3d COMMAND golden_image_tests_3d)
        set_tests_properties(golden_image_tests_3d PROPERTIES LABELS "golden;gpu")
    endif()
endif()

# Benchmarks
if(SPECTRA_BUILD_BENCHMARKS)
    function(add_spectra_bench name)
        add_executable(${name} bench/${name}.cpp)
        target_link_libraries(${name} PRIVATE spectra benchmark::benchmark benchmark::benchmark_main)
        target_compile_features(${name} PRIVATE cxx_std_20)
        target_include_directories(${name} PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/util
        )
    endfunction()

    add_spectra_bench(bench_decimation)
    add_spectra_bench(bench_render)
    add_spectra_bench(bench_ui)

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/bench/bench_phase2.cpp)
        add_spectra_bench(bench_phase2)
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/bench/bench_phase3.cpp)
        add_spectra_bench(bench_phase3)
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/bench/bench_3d.cpp)
        add_spectra_bench(bench_3d)
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/bench/bench_3d_phase3.cpp)
        add_spectra_bench(bench_3d_phase3)
    endif()

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/bench/bench_multi_window.cpp)
        add_spectra_bench(bench_multi_window)
    endif()
endif()
