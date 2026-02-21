# Unused Code Report

Generated on: Sat Feb 21 02:42:07 PM IST 2026
Project Root: /home/daniel/projects/Spectra

## Summary
- Unused Source Files: 172
- Unused Headers: 3
- Unused Targets: 2
- Unused Assets: 0
- Obsolete Code candidates: 15
- Structural Findings: 2

## Methodology
1. **Inventory**: Parsed `compile_commands.json`.
2. **Source Analysis**: filesystem vs compiled list.
3. **Header Analysis**: `#include` grep.
4. **Asset Analysis**: filename grep in source (checking for C++ var names for shaders).
5. **Obsolete Code**: regex for `#if 0`, `deprecated`, and manual structural analysis.

## 1. High-Impact Cleanup Candidates (Structural)
| Module/Symbol | Path | Confidence | Evidence |
|---------------|------|------------|----------|
| `Animator::add_timeline` | `src/anim/animator.cpp` | High | Method add_timeline is never called in the codebase. |
| `Renderer::update_frame_ubo` | `src/render/renderer.cpp` | High | Unused member function. Rendering uses per-axes UBO updates. |

## 2. Unused Source Files
| File Path | Recommendation |
|-----------|----------------|
| `build_cleanup/CMakeFiles/3.22.1/CompilerIdC/CMakeCCompilerId.c` | Delete |
| `build_cleanup/CMakeFiles/3.22.1/CompilerIdCXX/CMakeCXXCompilerId.cpp` | Delete |
| `build_cleanup/_deps/googletest-src/googlemock/src/gmock-all.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googlemock/src/gmock-cardinalities.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googlemock/src/gmock-internal-utils.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googlemock/src/gmock-matchers.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googlemock/src/gmock-spec-builders.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googlemock/src/gmock.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googlemock/src/gmock_main.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googlemock/test/gmock-actions_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googlemock/test/gmock-cardinalities_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googlemock/test/gmock-function-mocker_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googlemock/test/gmock-internal-utils_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googlemock/test/gmock-matchers-arithmetic_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googlemock/test/gmock-matchers-comparisons_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googlemock/test/gmock-matchers-containers_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googlemock/test/gmock-matchers-misc_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googlemock/test/gmock-more-actions_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googlemock/test/gmock-nice-strict_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googlemock/test/gmock-port_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googlemock/test/gmock-pp-string_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googlemock/test/gmock-pp_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googlemock/test/gmock-spec-builders_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googlemock/test/gmock_all_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googlemock/test/gmock_ex_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googlemock/test/gmock_leak_test_.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googlemock/test/gmock_link2_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googlemock/test/gmock_link_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googlemock/test/gmock_output_test_.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googlemock/test/gmock_stress_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googlemock/test/gmock_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/samples/sample1.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/samples/sample10_unittest.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/samples/sample1_unittest.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/samples/sample2.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/samples/sample2_unittest.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/samples/sample3_unittest.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/samples/sample4.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/samples/sample4_unittest.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/samples/sample5_unittest.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/samples/sample6_unittest.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/samples/sample7_unittest.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/samples/sample8_unittest.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/samples/sample9_unittest.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/src/gtest-all.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/src/gtest-assertion-result.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/src/gtest-death-test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/src/gtest-filepath.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/src/gtest-matchers.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/src/gtest-port.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/src/gtest-printers.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/src/gtest-test-part.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/src/gtest-typed-test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/src/gtest.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/src/gtest_main.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/googletest-break-on-failure-unittest_.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/googletest-catch-exceptions-test_.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/googletest-color-test_.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/googletest-death-test-test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/googletest-death-test_ex_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/googletest-env-var-test_.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/googletest-failfast-unittest_.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/googletest-filepath-test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/googletest-filter-unittest_.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/googletest-global-environment-unittest_.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/googletest-list-tests-unittest_.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/googletest-listener-test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/googletest-message-test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/googletest-options-test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/googletest-output-test_.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/googletest-param-test-invalid-name1-test_.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/googletest-param-test-invalid-name2-test_.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/googletest-param-test-test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/googletest-param-test2-test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/googletest-port-test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/googletest-printers-test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/googletest-setuptestsuite-test_.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/googletest-shuffle-test_.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/googletest-test-part-test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/googletest-throw-on-failure-test_.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/googletest-uninitialized-test_.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/gtest-typed-test2_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/gtest-typed-test_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/gtest-unittest-api_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/gtest_all_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/gtest_assert_by_exception_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/gtest_dirs_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/gtest_environment_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/gtest_help_test_.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/gtest_list_output_unittest_.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/gtest_main_unittest.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/gtest_no_test_unittest.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/gtest_pred_impl_unittest.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/gtest_premature_exit_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/gtest_prod_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/gtest_repeat_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/gtest_skip_in_environment_setup_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/gtest_skip_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/gtest_sole_header_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/gtest_stress_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/gtest_test_macro_stack_footprint_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/gtest_testbridge_test_.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/gtest_throw_on_failure_ex_test.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/gtest_unittest.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/gtest_xml_outfile1_test_.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/gtest_xml_outfile2_test_.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/gtest_xml_output_unittest_.cc` | Delete |
| `build_cleanup/_deps/googletest-src/googletest/test/production.cc` | Delete |
| `build_cleanup/_deps/imgui-src/backends/imgui_impl_allegro5.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/backends/imgui_impl_android.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/backends/imgui_impl_dx10.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/backends/imgui_impl_dx11.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/backends/imgui_impl_dx12.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/backends/imgui_impl_dx9.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/backends/imgui_impl_glfw.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/backends/imgui_impl_glut.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/backends/imgui_impl_opengl2.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/backends/imgui_impl_opengl3.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/backends/imgui_impl_sdl2.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/backends/imgui_impl_sdl3.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/backends/imgui_impl_sdlgpu3.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/backends/imgui_impl_sdlrenderer2.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/backends/imgui_impl_sdlrenderer3.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/backends/imgui_impl_vulkan.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/backends/imgui_impl_wgpu.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/backends/imgui_impl_win32.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/examples/example_allegro5/main.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/examples/example_android_opengl3/main.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/examples/example_glfw_opengl2/main.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/examples/example_glfw_opengl3/main.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/examples/example_glfw_vulkan/main.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/examples/example_glfw_wgpu/main.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/examples/example_glut_opengl2/main.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/examples/example_null/main.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/examples/example_sdl2_directx11/main.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/examples/example_sdl2_opengl2/main.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/examples/example_sdl2_opengl3/main.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/examples/example_sdl2_sdlrenderer2/main.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/examples/example_sdl2_vulkan/main.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/examples/example_sdl3_opengl3/main.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/examples/example_sdl3_sdlgpu3/main.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/examples/example_sdl3_sdlrenderer3/main.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/examples/example_sdl3_vulkan/main.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/examples/example_win32_directx10/main.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/examples/example_win32_directx11/main.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/examples/example_win32_directx12/main.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/examples/example_win32_directx9/main.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/examples/example_win32_opengl3/main.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/examples/example_win32_vulkan/main.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/examples/libs/usynergy/uSynergy.c` | Delete |
| `build_cleanup/_deps/imgui-src/imgui.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/imgui_demo.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/imgui_draw.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/imgui_tables.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/imgui_widgets.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/misc/cpp/imgui_stdlib.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/misc/fonts/binary_to_compressed_c.cpp` | Delete |
| `build_cleanup/_deps/imgui-src/misc/freetype/imgui_freetype.cpp` | Delete |
| `src/agent/main.cpp` | Delete |
| `src/daemon/figure_model.cpp` | Delete |
| `src/daemon/main.cpp` | Delete |
| `src/ui/app_multiproc.cpp` | Delete |
| `tests/bench/bench_3d.cpp` | Delete |
| `tests/bench/bench_3d_phase3.cpp` | Delete |
| `tests/bench/bench_decimation.cpp` | Delete |
| `tests/bench/bench_multi_window.cpp` | Delete |
| `tests/bench/bench_phase2.cpp` | Delete |
| `tests/bench/bench_phase3.cpp` | Delete |
| `tests/bench/bench_render.cpp` | Delete |
| `tests/bench/bench_ui.cpp` | Delete |
| `tests/unit/test_axes3d.cpp` | Delete |
| `tests/unit/test_figure_window_api.cpp` | Delete |

## 3. Unused Assets
| File Path | Recommendation |
|-----------|----------------|
| *None* | |

## 4. Unused Targets
| Target | Recommendation |
|--------|----------------|
| `spectra-backend` | Remove from CMakeLists |
| `spectra-window` | Remove from CMakeLists |

## 5. Unused Headers
| File Path | Recommendation |
|-----------|----------------|
| `build_cleanup/_deps/imgui-src/examples/example_allegro5/imconfig_allegro5.h` | Verify & Remove |
| `build_cleanup/_deps/imgui-src/misc/single_file/imgui_single_file.h` | Verify & Remove |
| `tests/util/validation_guard.hpp` | Verify & Remove |

## 6. Potential Obsolete Code Blocks
| File | Line | Type | Content |
|------|------|------|---------|
| `build_cleanup/_deps/imgui-src/backends/imgui_impl_win32.h` | 30 | #if 0 block | `// - Intentionally commented out in a '#if 0' bloc...` |
| `build_cleanup/_deps/imgui-src/backends/imgui_impl_win32.h` | 34 | #if 0 block | `#if 0...` |
| `build_cleanup/_deps/googletest-src/googletest/include/gtest/gtest-param-test.h` | 45 | #if 0 block | `#if 0...` |
| `build_cleanup/_deps/imgui-src/imstb_rectpack.h` | 301 | #if 0 block | `#if 0...` |
| `build_cleanup/_deps/googletest-src/googletest/include/gtest/gtest-typed-test.h` | 43 | #if 0 block | `#if 0...` |
| `build_cleanup/_deps/googletest-src/googletest/include/gtest/gtest-typed-test.h` | 121 | #if 0 block | `#if 0...` |
| `build_cleanup/_deps/imgui-src/imgui_tables.cpp` | 1409 | #if 0 block | `#if 0...` |
| `build_cleanup/_deps/imgui-src/imgui_tables.cpp` | 2617 | #if 0 block | `#if 0...` |
| `build_cleanup/_deps/imgui-src/imgui_tables.cpp` | 3505 | #if 0 block | `#if 0...` |
| `build_cleanup/_deps/imgui-src/imgui_widgets.cpp` | 10178 | #if 0 block | `#if 0...` |
| `build_cleanup/_deps/imgui-src/imgui_widgets.cpp` | 10331 | #if 0 block | `#if 0...` |
| `build_cleanup/_deps/imgui-src/imgui_widgets.cpp` | 10394 | #if 0 block | `#if 0...` |
| `build_cleanup/_deps/imgui-src/imstb_truetype.h` | 282 | #if 0 block | `#if 0...` |
| `build_cleanup/_deps/imgui-src/imstb_truetype.h` | 332 | #if 0 block | `#if 0...` |
| `build_cleanup/_deps/imgui-src/imstb_truetype.h` | 376 | #if 0 block | `#if 0...` |

## Next Actions
1. **Remove Animator/Timeline**: These are old animation classes superseded by `TimelineEditor`/`TransitionEngine`.
2. **Remove Unused Assets**: Delete the reported unused assets (if any remain).
3. **Clean Unused Targets**: Remove unused targets from CMakeLists.txt.
4. **Review Obsolete Blocks**: Check the TODOs and deprecated warnings.
