# Add a New Series Type

Add a new plot series type (e.g., bar chart, heatmap, contour).

## Steps

1. Define the series class header in `include/spectra/` if it's part of the public API
2. Implement the series in `src/core/` (inherit from Series or Series3D base)
3. Add rendering support:
   - Create shaders in `src/gpu/shaders/` if needed
   - Add pipeline in `src/render/vulkan/vk_pipeline.cpp`
   - Add draw method in `src/render/renderer.cpp`
4. Wire it into Axes/Axes3D with a creation method (e.g., `ax.bar(x, y)`)
5. Support dirty marking for incremental GPU uploads
6. Add unit tests in `tests/unit/`
7. Add a golden test case if the series has visual output
8. Create an example in `examples/`

## Conventions

- Use `std::span<const float>` for input data
- Support the builder pattern for configuration (`.color()`, `.label()`, etc.)
- Mark data as dirty when modified to trigger GPU re-upload
- Follow the existing Series/LineSeries/ScatterSeries pattern
