//! The smallest useful plot: two lines, a legend, then wait for the window.
//!
//! ```sh
//! cargo run --example basic
//! ```

use spectra_plot::prelude::*;

fn main() -> Result<()> {
    let session = Session::new()?;
    let fig = session.figure("Basic")?;
    let ax = fig.subplot(1, 1, 1)?;

    let x = linspace(0.0, 10.0, 500);
    let y1: Vec<f64> = x.iter().map(|v| v.sin()).collect();
    let y2: Vec<f64> = x.iter().map(|v| (v * 0.5).cos() * 0.8).collect();
    ax.plot(&x, &y1, "-")?.set_label("sin(x)")?;
    ax.plot(&x, &y2, "r--")?.set_label("0.8 cos(x/2)")?;
    ax.set_title("Hello from Rust")?
        .set_xlabel("x")?
        .set_ylabel("y")?;
    ax.legend(true)?.grid(true)?;

    session.show() // blocks until the window is closed
}
