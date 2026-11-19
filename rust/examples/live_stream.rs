//! Stream a signal into a line at 60 fps from your own loop.
//!
//! ```sh
//! cargo run --example live_stream
//! ```

use spectra_plot::*;

fn main() -> Result<()> {
    let session = Session::new()?;
    let fig = session.figure("Live stream")?;
    let ax = fig.subplot(1, 1, 1)?;
    ax.set_title("Close the window to stop")?
        .set_xlabel("time (s)")?;
    let signal = ax.line(&[0.0f64; 0], &[0.0f64; 0])?;
    signal.set_label("signal")?;
    let noise = ax.scatter(&[0.0f64; 0], &[0.0f64; 0])?;
    noise.set_label("samples")?.set_marker_size(3.0)?;
    ax.legend(true)?;
    fig.show()?;

    let mut pacer = FramePacer::new(60.0);
    let mut t = 0.0;
    while fig.is_visible() {
        t += pacer.pace(&session)?;
        let y = (t * 2.0).sin() + 0.3 * (t * 7.0).sin();
        signal.append(&[t], &[y])?;
        if (t * 10.0) as u64 % 3 == 0 {
            noise.append(&[t], &[y + 0.2 * ((t * 91.0).sin())])?;
        }
    }
    Ok(())
}
