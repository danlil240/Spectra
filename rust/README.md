# spectra-plot

Rust client for [Spectra](https://github.com/danlil240/Spectra), a GPU-accelerated plotting app
for 2D and 3D charts and live data.

Your program talks to the Spectra backend (`spectra-backend`) over a local socket. The backend
draws with Vulkan in its own windows, so plotting never blocks on rendering, and windows stay
open after your program exits. The crate is pure Rust: no C++ toolchain, bindgen or Vulkan SDK
is needed to build it.

```rust
use spectra_plot::{linspace, Session};

fn main() -> spectra_plot::Result<()> {
    let session = Session::new()?; // finds or starts spectra-backend
    let fig = session.figure("Waves")?;
    let ax = fig.subplot(1, 1, 1)?;

    let x = linspace(0.0, 10.0, 500);
    let y: Vec<f64> = x.iter().map(|v| v.sin()).collect();
    ax.plot(&x, &y, "r--")?.set_label("sin")?;
    ax.set_title("Hello from Rust")?.legend(true)?;

    fig.savefig("waves.png")?;
    session.show() // blocks until the window is closed
}
```

## Install

The crate is not on crates.io yet; add it from the Git tag that matches your Spectra install:

```sh
cargo add spectra-plot --git https://github.com/danlil240/Spectra --tag v0.4.2
```

## Getting the backend

Install Spectra from a [release](https://github.com/danlil240/Spectra/releases) (.deb, .rpm,
AppImage or macOS package) or build it from source. The backend runs on Linux and macOS. On
Windows, run your program and the backend inside WSL2; WSLg shows the windows. WSL's stock Mesa
only offers software Vulkan, so for GPU rendering build Mesa's D3D12 driver
(`-Dvulkan-drivers=microsoft-experimental`) and point `VK_ICD_FILENAMES` at its ICD file. A
native Windows build compiles but cannot connect yet. The client looks for the backend in this
order:

1. `$SPECTRA_SOCKET`: connect to that broker socket.
2. A running Spectra app or backend for your user.
3. Launch `spectra-backend` from `$SPECTRA_BACKEND_PATH`, `$SPECTRA_BACKEND`, next to your
   executable, or on `PATH`.

The crate version follows the Spectra version; use the same release for both.

## What it covers

The API mirrors the Python client.

- Figures, subplot grids (shared axes), tabs and windows, figure styles, suptitles, layouts.
- Lines, scatter, MATLAB format strings (`"r--o"`), step and stairs, `fplot`, reference lines.
- Bars (grouped, stacked, horizontal, categorical), histograms, box plots, violins, bands,
  stems and error bars.
- Images (colormapped or RGB), heatmaps with labelled cells, contour lines and filled contours.
- 3D lines, scatter and surfaces with colormaps, lighting, fog and contour projection.
- Text and arrow annotations with boxes, halos and rotation.
- Axis limits, log scales, datetime axes, labels, grid and legend, with autoscaling done on the
  client like the Python package.
- Live data: `Series::append`, `FramePacer` for your own loop, and `Figure::animate` for a
  timed callback.
- Topics: `Publisher` streams samples by name, `Axes::subscribe_topic` plots them live, with
  the axes following the data until you set limits.
- Knobs: sliders in the window that your code reads with `Session::knob`.
- Export to PNG and SVG files or bytes, offscreen.

Data can be any slice, `Vec` or array of `f64`, `f32` or integers. With the `ndarray` feature,
`ndarray` arrays work too.

## Examples

```sh
cargo run --example basic             # two lines and a legend
cargo run --example gallery -- out    # every chart type, saved as PNG
cargo run --example live_stream       # append at 60 fps from your own loop
cargo run --example animate           # timed animation driven by a slider
cargo run --example topic_publisher   # publish topics...
cargo run --example topic_subscriber  # ...and plot them in a window
```

## Development

Unit tests need nothing. The end-to-end tests run against a real backend when
`SPECTRA_BACKEND_PATH` is set, and need a display (`xvfb-run` works):

```sh
cmake --build build --target spectra-backend spectra-window
SPECTRA_BACKEND_PATH=$PWD/build/spectra-backend xvfb-run -a cargo test --manifest-path rust/Cargo.toml
```

`src/generated/` is FlatBuffers code generated from `src/ipc/schemas/spectra_ipc.fbs`. After
changing the schema, run `rust/generate_flatbuffers.sh`; CI fails when it is stale.

## License

MIT
