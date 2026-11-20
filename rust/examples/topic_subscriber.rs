//! Plot the topics published by `topic_publisher` in a window.
//!
//! ```sh
//! cargo run --example topic_subscriber
//! ```

use spectra_plot::*;

fn main() -> Result<()> {
    let session = Session::new()?;
    let fig = session.figure("Topics")?;
    let ax = fig.subplot(1, 1, 1)?;
    ax.set_title("demo/imu/accel_x")?.set_xlabel("time (s)")?;
    ax.subscribe_topic("demo/imu/accel_x")?
        .set_label("accel_x")?;
    ax.legend(true)?;
    for topic in session.list_topics()? {
        println!(
            "{} ({}{})",
            topic.name,
            if topic.is_3d { "3D" } else { "2D" },
            if topic.unit.is_empty() {
                String::new()
            } else {
                format!(", {}", topic.unit)
            }
        );
    }
    session.show()
}
