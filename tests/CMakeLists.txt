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
option(PLOTIX_BUILD_BENCHMARKS "Build performance benchmarks" OFF)
if(PLOTIX_BUILD_BENCHMARKS)
    FetchContent_Declare(
        googlebenchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        v1.8.3
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googlebenchmark)
endif()

# Helper function
function(add_plotix_test subdir name)
    string(REPLACE "/" "_" target_name "${subdir}_${name}")
    add_executable(${target_name} ${subdir}/${name}.cpp)
    target_link_libraries(${target_name} PRIVATE plotix GTest::gtest_main)
    target_compile_features(${target_name} PRIVATE cxx_std_20)
    # Tests need access to internal src/ headers (e.g. core/transform.hpp)
    target_include_directories(${target_name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

# Unit tests
add_plotix_test(unit test_transform)
add_plotix_test(unit test_layout)
add_plotix_test(unit test_timeline)
add_plotix_test(unit test_easing)
add_plotix_test(unit test_ring_buffer)
add_plotix_test(unit test_input)
add_plotix_test(unit test_series_data)
add_plotix_test(unit test_command_queue)
add_plotix_test(unit test_decimation)
add_plotix_test(unit test_filters)
add_plotix_test(unit test_tick_generation)
add_plotix_test(unit test_series_visibility)
add_plotix_test(unit test_font_atlas)
add_plotix_test(unit test_svg_export)

# Golden image tests (require Vulkan — headless via lavapipe in CI)
option(PLOTIX_BUILD_GOLDEN_TESTS "Build golden image regression tests" ON)
if(PLOTIX_BUILD_GOLDEN_TESTS)
    add_executable(golden_image_tests golden/golden_test.cpp)
    target_link_libraries(golden_image_tests PRIVATE plotix GTest::gtest_main)
    target_compile_features(golden_image_tests PRIVATE cxx_std_20)
    target_include_directories(golden_image_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/golden
    )
    add_test(NAME golden_image_tests COMMAND golden_image_tests)

    # Label golden tests so CI can run them separately
    set_tests_properties(golden_image_tests PROPERTIES LABELS "golden")
endif()

# Benchmarks
if(PLOTIX_BUILD_BENCHMARKS)
    function(add_plotix_bench name)
        add_executable(${name} bench/${name}.cpp)
        target_link_libraries(${name} PRIVATE plotix benchmark::benchmark benchmark::benchmark_main)
        target_compile_features(${name} PRIVATE cxx_std_20)
        target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    endfunction()

    add_plotix_bench(bench_decimation)
    add_plotix_bench(bench_render)
endif()
