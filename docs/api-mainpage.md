# Spectra C++ API Reference {#mainpage}

GPU-accelerated scientific plotting for C++20 and Python.

This reference covers the public headers in `include/spectra/`. Good places to start:

- **Easy API** (`spectra/easy.hpp`) — MATLAB-style free functions such as `spectra::plot()`,
  `spectra::subplot()` and `spectra::show()`, acting on an implicit current figure.
- **spectra::App** — owns figures and windows and runs the event loop.
- **spectra::Figure** — a canvas with a grid of 2D and 3D axes; export with `save_png()`.
- **spectra::Axes** — a 2D coordinate system; creates line, scatter, statistical and shape series.
- **spectra::Series**, **spectra::LineSeries**, **spectra::ScatterSeries** — the plotted data
  and its style.

```cpp
#include <spectra/easy.hpp>

int main()
{
    std::vector<float> x = {0, 1, 2, 3, 4};
    std::vector<float> y = {0, 1, 4, 9, 16};
    spectra::plot(x, y, "r--o").label("y = x^2");
    spectra::title("Hello Spectra");
    spectra::show();
}
```

Guides, examples and adapter documentation are on the
[documentation site](https://danlil240.github.io/Spectra/). Spectra is a 0.x public preview, so
the API may change before 1.0.
