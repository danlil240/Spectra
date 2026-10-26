# Spectra C++ Examples

Every `.cpp` file in this directory is a standalone program that links against `spectra`.
Python examples live in [`python/examples/`](../python/examples/README.md).

## Build and run

Examples are built when `SPECTRA_BUILD_EXAMPLES=ON` (the default):

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSPECTRA_USE_ROS2=OFF
cmake --build build -j$(nproc)                      # everything
cmake --build build --target basic_line             # or a single example

./build/examples/basic_line
```

Binaries land in `build/examples/<name>`. Examples that open a window need a Vulkan driver and a
display (X11 or Wayland). Offscreen examples (`offscreen_export`, `easy_embed_demo`,
`embed_cpp_demo`, `video_record`) also run headless.

## Getting started

| Example | What it shows |
|---|---|
| [`basic_line.cpp`](basic_line.cpp) | Minimal line plot of a sine wave with title and labels. |
| [`easy_api_demo.cpp`](easy_api_demo.cpp) | The one-header Easy API (`spectra/easy.hpp`) at every complexity level. |
| [`multi_subplot.cpp`](multi_subplot.cpp) | Two stacked subplots with independent data. |
| [`comprehensive_subplot_demo.cpp`](comprehensive_subplot_demo.cpp) | Four different plot types in one figure. |
| [`plot_styles_demo.cpp`](plot_styles_demo.cpp) | MATLAB-style format strings: line styles, markers, colors. |
| [`next_level_scientific.cpp`](next_level_scientific.cpp) | Colormapped scatter and a native uncertainty band. |
| [`stats_demo.cpp`](stats_demo.cpp) | Statistical series: box plots, violins, histograms and bars. |
| [`shapes_demo.cpp`](shapes_demo.cpp) | 2D shape primitives (rectangles, circles, polygons, arrows). |
| [`logger_example.cpp`](logger_example.cpp) | Configuring the Spectra logger (console and file sinks). |

## Interaction and UI

| Example | What it shows |
|---|---|
| [`interactive_ref_demo.cpp`](interactive_ref_demo.cpp) | Drag sliders to move reference lines and retune a curve live. |
| [`knob_demo.cpp`](knob_demo.cpp) | Interactive parameter knobs in a floating panel. |
| [`axes_menu_demo.cpp`](axes_menu_demo.cpp) | Multi-subplot figure for exploring the per-axes menus (right-click an axes). |
| [`empty_launch_csv.cpp`](empty_launch_csv.cpp) | Start with an empty figure and load data via *Data -> Load from CSV*. |
| [`shortcut_config_demo.cpp`](shortcut_config_demo.cpp) | Custom keybindings and shortcut save/load. |
| [`shortcut_usage_demo.cpp`](shortcut_usage_demo.cpp) | How shortcut persistence works (conceptual walkthrough). |
| [`plugin_api_demo.cpp`](plugin_api_demo.cpp) | Using the plugin API for external extensions. |
| [`plugins/`](plugins/) | Sample runtime plugins: CSV export, crosshair overlay, heatmap series, smoothing transform. |

## Windows and figures

| Example | What it shows |
|---|---|
| [`multi_figure_demo.cpp`](multi_figure_demo.cpp) | Several figures, each in its own OS window. |
| [`multi_window_tabs_demo.cpp`](multi_window_tabs_demo.cpp) | Easy-API tab control: figures as tabs in one window. |
| [`multi_window_demo.cpp`](multi_window_demo.cpp) | Several figures managed by one `App` (multi-window groundwork). |
| [`multiproc_demo.sh`](multiproc_demo.sh) | Launch `spectra-backend` plus one or more `spectra-window` agents. |

## Animation and streaming

| Example | What it shows |
|---|---|
| [`live_stream.cpp`](live_stream.cpp) | Append a point per frame to simulate a sensor stream. |
| [`easy_realtime_demo.cpp`](easy_realtime_demo.cpp) | Real-time multi-signal sensor simulation with the Easy API. |
| [`animated_scatter.cpp`](animated_scatter.cpp) | Scatter points animated with a frame callback. |
| [`advanced_animation_demo.cpp`](advanced_animation_demo.cpp) | Keyframe interpolator bound to series properties. |
| [`timeline_animation_demo.cpp`](timeline_animation_demo.cpp) | Timeline editor + keyframes + curve editor (`T`, `Shift+T`, `Space`). |
| [`timeline_curve_demo.cpp`](timeline_curve_demo.cpp) | Timeline and animation curve editor integration. |
| [`topic_publisher.cpp`](topic_publisher.cpp) | Publish a named topic to `spectra-backend`, with or without a UI open. |

## 3D

See also [`README_3D.md`](README_3D.md).

| Example | What it shows |
|---|---|
| [`simple_3d_scatter.cpp`](simple_3d_scatter.cpp) | 3D scatter plot of a spiral. |
| [`parametric_3d_line.cpp`](parametric_3d_line.cpp) | Parametric 3D line (trefoil knot). |
| [`surface_3d.cpp`](surface_3d.cpp) | Surface plot of `z = sin(x) * cos(y)`. |
| [`lit_surface_demo.cpp`](lit_surface_demo.cpp) | Lit surfaces with custom materials. |
| [`transparency_demo.cpp`](transparency_demo.cpp) | Transparent surfaces, wireframes and point clouds. |
| [`axes3d_demo.cpp`](axes3d_demo.cpp) | Configuring 3D axes and grid planes. |
| [`shapes3d_demo.cpp`](shapes3d_demo.cpp) | 3D shape primitives (box, sphere, ...). |
| [`demo_3d.cpp`](demo_3d.cpp) | Animated 3D scatter, line and surface subplots. |
| [`camera_animation_3d.cpp`](camera_animation_3d.cpp) | Animated orbit camera around a 3D scene. |
| [`camera_animator_demo.cpp`](camera_animator_demo.cpp) | Camera animator keyframes. |
| [`mode_transition_demo.cpp`](mode_transition_demo.cpp) | Animated 2D <-> 3D view switching. |
| [`realtime_3d_stream.cpp`](realtime_3d_stream.cpp) | Real-time 3D data stream. |
| [`mind_blowing_3d.cpp`](mind_blowing_3d.cpp) | Animated 3D showcase with chaotic particle motion. |

## Export and embedding

| Example | What it shows |
|---|---|
| [`offscreen_export.cpp`](offscreen_export.cpp) | Headless render to PNG without a window. |
| [`video_record.cpp`](video_record.cpp) | Record an animation to `output.mp4` (needs `SPECTRA_USE_FFMPEG=ON`). |
| [`easy_embed_demo.cpp`](easy_embed_demo.cpp) | `spectra::easy_embed` one-liner offscreen rendering. |
| [`embed_cpp_demo.cpp`](embed_cpp_demo.cpp) | C++ embedding API: render a plot into your own buffer. |

## Optional integrations

These are only built when the matching CMake option is enabled.

| Example | Option | What it shows |
|---|---|---|
| [`eigen_demo.cpp`](eigen_demo.cpp) | `SPECTRA_USE_EIGEN=ON` | Pass Eigen vectors directly to Spectra. |
| [`qt_embed_demo.cpp`](qt_embed_demo.cpp) | `SPECTRA_USE_QT=ON`, `SPECTRA_BUILD_QT_EXAMPLE=ON` | Embed a Spectra canvas in a Qt6 widget (also builds `qt_multi_canvas_demo`). |
| [`ros2_demo.cpp`](ros2_demo.cpp) | `SPECTRA_USE_ROS2=ON` | Tour of the ROS2 adapter. |
| [`ros2_ui_preview.cpp`](ros2_ui_preview.cpp) | (ImGui build) | Preview of the `spectra-ros` panels with synthetic data, no ROS2 needed. |
| [`ros2_launch/`](ros2_launch/) | `SPECTRA_USE_ROS2=ON` | ROS2 launch files for common `spectra-ros` layouts. |
| [`webgpu_demo.cpp`](webgpu_demo.cpp) | `SPECTRA_USE_WEBGPU=ON` | WebGPU backend, native (Dawn) or browser (Emscripten, uses `webgpu_shell.html`). |

Data file: [`sample_data.csv`](sample_data.csv) is a small dataset for the CSV loader.

Interactive programs used for manual UI checks live in [`tests/manual/`](../tests/manual/).
