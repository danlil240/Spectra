//! # spectra-plot
//!
//! Rust client for [Spectra](https://github.com/danlil240/Spectra), a
//! GPU-accelerated plotting app. Your program talks to the Spectra backend
//! (`spectra-backend`) over a local socket; the backend renders with Vulkan in
//! its own windows, so plotting never blocks on drawing and windows survive
//! your program exiting.
//!
//! ```no_run
//! use spectra_plot::{linspace, Session};
//!
//! let session = Session::new()?; // finds or starts spectra-backend
//! let fig = session.figure("Waves")?;
//! let ax = fig.subplot(1, 1, 1)?;
//! let x = linspace(0.0, 10.0, 500);
//! let y: Vec<f64> = x.iter().map(|v| v.sin()).collect();
//! ax.plot(&x, &y, "r-")?.set_label("sin")?;
//! ax.set_title("Hello from Rust")?;
//! ax.legend(true)?;
//! fig.savefig("waves.png")?;
//! session.show()?; // block until the window is closed
//! # Ok::<(), spectra_plot::Error>(())
//! ```
//!
//! The backend is found through `$SPECTRA_SOCKET`, a running Spectra app, or
//! by launching `spectra-backend` from `$SPECTRA_BACKEND_PATH` or `PATH`.

#![warn(missing_docs)]

mod animation;
mod annotation;
mod axes;
mod codec;
mod color;
mod data;
mod error;
mod figure;
mod generated;
mod launcher;
mod protocol;
mod series;
mod session;
mod style;
mod topic;
mod transport;

pub use animation::{Animator, FramePacer};
pub use annotation::{Halo, TextAnnotation, TextBox, TextOptions};
pub use axes::{
    Axes, AxesBatch, BandOptions, BarOptions, BoxOptions, ContourOptions, ErrorBar,
    ErrorBarOptions, ErrorBarStyle, ErrorSpec, FillStyle, HeatmapOptions, HistOptions,
    ImageOptions, StemOptions, ViolinOptions,
};
pub use codec::TopicInfo;
pub use color::{Color, ColorSpec, IntoColor};
pub use data::{arange, linspace, Scalar, Values};
pub use error::{Error, Result};
pub use figure::{ExportOptions, Figure, ImageFormat};
pub use series::{Series, SeriesKind};
pub use session::{AnimFrame, Event, Session, SessionOptions};
pub use style::{
    BarMode, DenseMode, DrawStyle, Format, HAlign, LightMode, LineStyle, Marker, PlotStyle, Scale,
    Share, VAlign,
};
pub use topic::{Publisher, PublisherOptions, TopicKind};

/// Everything needed for typical plotting code: `use spectra_plot::prelude::*;`.
pub mod prelude {
    pub use crate::{
        arange, linspace, Axes, Color, Error, Figure, Format, IntoColor, LineStyle, Marker,
        PlotStyle, Publisher, Result, Scale, Series, Session, TextOptions,
    };
}
