//! Publish samples to named topics. Open the Spectra app, or run
//! `topic_subscriber`, to plot them live; the publisher keeps going (and
//! reconnects) if the app is closed and reopened.
//!
//! ```sh
//! cargo run --example topic_publisher
//! ```

use std::time::{Duration, Instant};

use spectra_plot::*;

fn main() -> Result<()> {
    let mut accel = Publisher::with_options(
        "demo/imu/accel_x",
        PublisherOptions {
            unit: "m/s^2".into(),
            ..Default::default()
        },
    )?;
    let mut pos = Publisher::with_options(
        "demo/imu/position",
        PublisherOptions {
            kind: TopicKind::Scalar3D,
            unit: "m".into(),
            ..Default::default()
        },
    )?;
    println!("publishing to demo/imu/accel_x and demo/imu/position (Ctrl+C to stop)");

    let start = Instant::now();
    loop {
        let t = start.elapsed().as_secs_f64();
        accel.publish(t, (t * 3.0).sin() * 9.81)?;
        pos.publish3(t.cos(), t.sin(), t * 0.1)?;
        std::thread::sleep(Duration::from_millis(10));
    }
}
