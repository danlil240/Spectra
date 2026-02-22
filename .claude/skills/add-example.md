# Add an Example Program

Create a new runnable example demonstrating a Spectra feature.

## Steps

1. Create `examples/<name>.cpp` following existing example patterns
2. Add the build target in `examples/CMakeLists.txt`
3. Include `<spectra/spectra.hpp>` (or `<spectra/easy.hpp>` for easy API)
4. Keep examples self-contained and focused on one feature
5. Build and verify it runs: `./build/examples/<name>`

## Conventions

- Use descriptive filenames: `animated_scatter.cpp`, `live_stream.cpp`
- Include comments explaining the demonstrated feature
- Use `spectra::App` for full control or `spectra::plot()`/`spectra::show()` for easy API
- Keep data generation inline (no external data files unless demonstrating import)
- Link against `spectra` target in CMake
