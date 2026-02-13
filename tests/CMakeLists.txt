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
