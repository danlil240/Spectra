//! Animate a travelling wave and read a slider from the window.
//!
//! Drag the "speed" knob in the window's PARAMETERS panel while it runs.
//!
//! ```sh
//! cargo run --example animate
//! ```

use spectra_plot::*;

fn main() -> Result<()> {
    let session = Session::new()?;
    let fig = session.figure("Animation")?;
    let ax = fig.subplot(1, 1, 1)?;
    ax.set_xlim(0.0, 10.0)?.set_ylim(-1.5, 1.5)?;
    fig.add_knob("speed", 1.0, 0.0, 5.0)?;

    let x = linspace(0.0, 10.0, 400);
    let wave = ax.line(&x, &x)?;
    wave.set_color("tab:orange")?.set_line_width(3.0)?;

    let knobs = session.clone();
    let mut phase = 0.0;
    fig.animate(60.0, 0.0, move |frame| {
        let speed = knobs.knob("speed").ok().flatten().unwrap_or(1.0) as f64;
        phase += speed * frame.dt as f64 * 3.0;
        let y: Vec<f64> = x
            .iter()
            .map(|v| (v - phase).sin() * (-0.05 * v).exp())
            .collect();
        let _ = wave.set_data(&x, &y);
    })?;

    session.show()
}
