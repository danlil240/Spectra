//! Figures: windows holding a grid of subplots.

use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use crate::animation::Animator;
use crate::axes::{Axes, AxesState};
use crate::codec::{self, PropertyUpdate};
use crate::error::{Error, Result};
use crate::protocol as p;
use crate::session::{lock, AnimEntry, AnimFrame, Session};
use crate::style::{PlotStyle, Share};

pub(crate) struct FigureState {
    pub id: u64,
    pub title: Mutex<String>,
    pub size: (u32, u32),
    pub visible: AtomicBool,
    pub shown_once: AtomicBool,
    pub destroyed: AtomicBool,
    pub window_id: AtomicU64,
    pub axes: Mutex<Vec<Arc<AxesState>>>,
    pub suptitle: Mutex<String>,
    /// (constrained layout, share x, share y)
    pub layout: Mutex<(bool, Share, Share)>,
    pub style: Mutex<Option<PlotStyle>>,
}

impl FigureState {
    pub fn new(id: u64, title: &str, size: (u32, u32)) -> Self {
        FigureState {
            id,
            title: Mutex::new(title.to_string()),
            size,
            visible: AtomicBool::new(false),
            shown_once: AtomicBool::new(false),
            destroyed: AtomicBool::new(false),
            window_id: AtomicU64::new(0),
            axes: Mutex::new(Vec::new()),
            suptitle: Mutex::new(String::new()),
            layout: Mutex::new((true, Share::None, Share::None)),
            style: Mutex::new(None),
        }
    }
}

/// Image format for [`Figure::savefig_with`].
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum ImageFormat {
    /// Pick from the file extension (`.png`, `.svg` or `.pdf`; PNG when
    /// there is none).
    #[default]
    Auto,
    /// PNG raster image.
    Png,
    /// SVG vector image.
    Svg,
    /// PDF document.
    Pdf,
}

/// Options for [`Figure::savefig_with`].
#[derive(Debug, Clone, Default)]
pub struct ExportOptions {
    /// Image width in pixels (0 = the figure size).
    pub width: u32,
    /// Image height in pixels (0 = the figure size).
    pub height: u32,
    /// File format.
    pub format: ImageFormat,
    /// Theme for this file, e.g. `"light"` or `"dark"` (empty = the saved theme).
    pub theme: String,
}

/// A figure owned by the backend: a window with a grid of subplots.
///
/// Cheap to clone; clones refer to the same figure.
#[derive(Clone)]
pub struct Figure {
    pub(crate) session: Session,
    pub(crate) state: Arc<FigureState>,
}

impl std::fmt::Debug for Figure {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Figure")
            .field("id", &self.id())
            .field("title", &self.title())
            .finish()
    }
}

impl PartialEq for Figure {
    fn eq(&self, other: &Self) -> bool {
        Arc::ptr_eq(&self.state, &other.state)
    }
}

impl Figure {
    /// Backend figure id.
    pub fn id(&self) -> u64 {
        self.state.id
    }

    /// The session this figure belongs to.
    pub fn session(&self) -> &Session {
        &self.session
    }

    /// Window title.
    pub fn title(&self) -> String {
        lock(&self.state.title).clone()
    }

    /// Size the figure was created with, in pixels.
    pub fn size(&self) -> (u32, u32) {
        self.state.size
    }

    fn prop(&self, name: &str) -> PropertyUpdate {
        PropertyUpdate::new(self.state.id, name)
    }

    /// Change the window title.
    pub fn set_title(&self, title: &str) -> Result<&Self> {
        self.session.update(self.prop("title").s(title))?;
        *lock(&self.state.title) = title.to_string();
        Ok(self)
    }

    /// Title drawn centred above the subplot grid (`""` removes it).
    pub fn suptitle(&self, text: &str) -> Result<&Self> {
        self.session.update(self.prop("suptitle").s(text))?;
        *lock(&self.state.suptitle) = text.to_string();
        Ok(self)
    }

    /// The current figure title above the subplots.
    pub fn get_suptitle(&self) -> String {
        lock(&self.state.suptitle).clone()
    }

    /// Apply a built-in plot style; existing and later axes and series follow it.
    pub fn apply_style(&self, style: PlotStyle) -> Result<&Self> {
        self.session.update(self.prop("style").s(style.name()))?;
        *lock(&self.state.style) = Some(style);
        Ok(self)
    }

    /// The style applied with [`Figure::apply_style`], if any.
    pub fn style(&self) -> Option<PlotStyle> {
        *lock(&self.state.style)
    }

    /// Change the subplot layout. `constrained` sizes margins from the labels
    /// (on by default); `sharex` / `sharey` link axis limits between subplots
    /// in the window. `None` keeps the current value.
    pub fn set_layout(
        &self,
        constrained: Option<bool>,
        sharex: Option<Share>,
        sharey: Option<Share>,
    ) -> Result<&Self> {
        let (c, sx, sy) = {
            let mut l = lock(&self.state.layout);
            if let Some(c) = constrained {
                l.0 = c;
            }
            if let Some(s) = sharex {
                l.1 = s;
            }
            if let Some(s) = sharey {
                l.2 = s;
            }
            *l
        };
        self.session.update(self.prop("figure_layout").f3(
            if c { 1.0 } else { 0.0 },
            sx.wire() as f64,
            sy.wire() as f64,
        ))?;
        Ok(self)
    }

    /// Turn on constrained layout (label sizes go into the subplot spacing).
    pub fn tight_layout(&self) -> Result<&Self> {
        self.set_layout(Some(true), None, None)
    }

    fn create_axes(&self, rows: i32, cols: i32, index: i32, is_3d: bool) -> Result<Axes> {
        if rows < 1 || cols < 1 || index < 1 || index > rows * cols {
            return Err(Error::invalid(format!(
                "subplot({rows}, {cols}, {index}): rows and cols must be >= 1 and index in 1..={}",
                rows.max(0) * cols.max(0)
            )));
        }
        self.request_axes(rows, cols, index, is_3d, -1)
    }

    /// Ask the backend for an axes (`twin_of` >= 0: the twinx() twin of that
    /// axes) and track it.
    pub(crate) fn request_axes(
        &self,
        rows: i32,
        cols: i32,
        index: i32,
        is_3d: bool,
        twin_of: i32,
    ) -> Result<Axes> {
        let payload = codec::encode_create_axes(self.state.id, rows, cols, index, is_3d, twin_of);
        let resp = self.session.request(p::REQ_CREATE_AXES, &payload)?;
        let axes_index = codec::decode_axes_created(&resp.payload)?;
        let mut list = lock(&self.state.axes);
        if let Some(existing) = list.iter().find(|a| a.index == axes_index) {
            return Ok(Axes::from_state(self.clone(), existing.clone()));
        }
        let mut st = AxesState::new(self.state.id, axes_index, is_3d);
        st.twin_of = twin_of;
        let state = Arc::new(st);
        list.push(state.clone());
        drop(list);
        Ok(Axes::from_state(self.clone(), state))
    }

    /// Create (or get) 2D subplot `index` (1-based, row-major) of a `rows` x `cols` grid.
    pub fn subplot(&self, rows: i32, cols: i32, index: i32) -> Result<Axes> {
        self.create_axes(rows, cols, index, false)
    }

    /// Create (or get) a 3D subplot.
    pub fn subplot3d(&self, rows: i32, cols: i32, index: i32) -> Result<Axes> {
        self.create_axes(rows, cols, index, true)
    }

    /// Create a whole `rows` x `cols` grid of 2D axes, in row-major order.
    pub fn subplots(&self, rows: i32, cols: i32) -> Result<Vec<Axes>> {
        (1..=rows * cols)
            .map(|i| self.subplot(rows, cols, i))
            .collect()
    }

    /// Like [`Figure::subplots`], with linked limits like matplotlib's
    /// `subplots(sharex=..., sharey=...)`: the window pans/zooms them together
    /// and automatic limits fit the data of the whole group.
    pub fn subplots_shared(
        &self,
        rows: i32,
        cols: i32,
        sharex: Share,
        sharey: Share,
    ) -> Result<Vec<Axes>> {
        let axes = self.subplots(rows, cols)?;
        self.set_layout(None, Some(sharex), Some(sharey))?;
        crate::axes::link_shared(&axes, cols.max(1) as usize, sharex, sharey);
        Ok(axes)
    }

    /// Every axes of this figure, in creation order.
    pub fn axes(&self) -> Vec<Axes> {
        lock(&self.state.axes)
            .iter()
            .map(|a| Axes::from_state(self.clone(), a.clone()))
            .collect()
    }

    /// Whether the figure's window is open.
    pub fn is_visible(&self) -> bool {
        self.state.visible.load(Ordering::SeqCst)
    }

    /// Window the figure is shown in (0 before [`Figure::show`]).
    pub fn window_id(&self) -> u64 {
        self.state.window_id.load(Ordering::SeqCst)
    }

    /// Open the figure in a new window (returns immediately; see [`Session::show`]).
    pub fn show(&self) -> Result<&Self> {
        self.show_in_window(0)
    }

    /// Show the figure as a tab in the window of `other` (which must be shown).
    pub fn show_as_tab_of(&self, other: &Figure) -> Result<&Self> {
        let wid = other.window_id();
        if wid == 0 {
            return Err(Error::invalid(
                "the other figure is not shown in a window yet",
            ));
        }
        self.show_in_window(wid)
    }

    /// Show the figure in window `window_id` (0 = a new window).
    pub fn show_in_window(&self, window_id: u64) -> Result<&Self> {
        let resp = self
            .session
            .request(p::REQ_SHOW, &codec::encode_show(self.state.id, window_id))?;
        if resp.header.window_id != 0 {
            self.state
                .window_id
                .store(resp.header.window_id, Ordering::SeqCst);
        }
        self.state.visible.store(true, Ordering::SeqCst);
        self.state.shown_once.store(true, Ordering::SeqCst);
        Ok(self)
    }

    /// Write the figure to a PNG, SVG or PDF file (by extension) and return its
    /// absolute path. The backend renders offscreen; no window is needed.
    pub fn savefig(&self, path: impl AsRef<Path>) -> Result<PathBuf> {
        self.savefig_with(path, &ExportOptions::default())
    }

    /// [`Figure::savefig`] with a size, format and theme.
    pub fn savefig_with(&self, path: impl AsRef<Path>, opts: &ExportOptions) -> Result<PathBuf> {
        let path = path.as_ref();
        let target = if path.is_absolute() {
            path.to_path_buf()
        } else {
            std::env::current_dir()?.join(path)
        };
        let format = match opts.format {
            ImageFormat::Auto => {
                let ext = target
                    .extension()
                    .map(|e| e.to_string_lossy().to_ascii_lowercase())
                    .unwrap_or_default();
                if !ext.is_empty() && !matches!(ext.as_str(), "png" | "svg" | "pdf") {
                    // Never write PNG bytes under a .jpg/.eps/... name.
                    return Err(Error::invalid(format!(
                        "cannot save .{ext} files; use a .png, .svg or .pdf name or set ExportOptions::format"
                    )));
                }
                ""
            }
            ImageFormat::Png => "png",
            ImageFormat::Svg => "svg",
            ImageFormat::Pdf => "pdf",
        };
        let target_str = target.to_str().ok_or_else(|| {
            Error::invalid(format!("path is not valid UTF-8: {}", target.display()))
        })?;
        let payload = codec::encode_export_figure(
            self.state.id,
            target_str,
            format,
            opts.width,
            opts.height,
            &opts.theme,
        );
        self.session.request(p::REQ_EXPORT_FIGURE, &payload)?;
        Ok(target)
    }

    fn render_bytes(
        &self,
        format: ImageFormat,
        ext: &str,
        width: u32,
        height: u32,
    ) -> Result<Vec<u8>> {
        let path = std::env::temp_dir().join(format!(
            "spectra-rs-{}-{}-{}.{ext}",
            std::process::id(),
            self.state.id,
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map(|d| d.as_nanos())
                .unwrap_or(0)
        ));
        let opts = ExportOptions {
            width,
            height,
            format,
            theme: String::new(),
        };
        self.savefig_with(&path, &opts)?;
        let bytes = std::fs::read(&path);
        let _ = std::fs::remove_file(&path);
        Ok(bytes?)
    }

    /// The figure rendered offscreen as PNG bytes (0 x 0 = the figure size).
    pub fn to_png(&self, width: u32, height: u32) -> Result<Vec<u8>> {
        self.render_bytes(ImageFormat::Png, "png", width, height)
    }

    /// The figure rendered as an SVG document.
    pub fn to_svg(&self) -> Result<String> {
        let bytes = self.render_bytes(ImageFormat::Svg, "svg", 0, 0)?;
        String::from_utf8(bytes).map_err(|e| Error::protocol(format!("SVG is not UTF-8: {e}")))
    }

    /// Close the window but keep the figure; [`Figure::show`] opens it again.
    pub fn close_window(&self) -> Result<()> {
        self.session.request(
            p::REQ_CLOSE_FIGURE,
            &codec::encode_close_figure(self.state.id),
        )?;
        self.state.visible.store(false, Ordering::SeqCst);
        Ok(())
    }

    /// Destroy the figure in the backend (its window closes).
    pub fn close(&self) -> Result<()> {
        self.session.request(
            p::REQ_DESTROY_FIGURE,
            &codec::encode_destroy_figure(self.state.id),
        )?;
        self.state.visible.store(false, Ordering::SeqCst);
        self.state.destroyed.store(true, Ordering::SeqCst);
        self.session.unregister_animator(self.state.id);
        Ok(())
    }

    /// Add an interactive knob (a slider in the window's PARAMETERS panel).
    /// Read it back with [`Session::knob_values`].
    pub fn add_knob(&self, name: &str, value: f32, min: f32, max: f32) -> Result<&Self> {
        if name.is_empty() {
            return Err(Error::invalid("knob name must not be empty"));
        }
        if min.partial_cmp(&max) != Some(std::cmp::Ordering::Less) || !value.is_finite() {
            return Err(Error::invalid(
                "knob range needs min < max and a finite value",
            ));
        }
        self.session.update(
            self.prop("add_knob")
                .f3(value as f64, min as f64, max as f64)
                .s(name),
        )?;
        Ok(self)
    }

    /// Run `on_frame` at `fps` frames per second for `duration` seconds
    /// (0 = until [`Animator::stop`]). Frames run while [`Session::show`],
    /// [`Session::sleep`] or [`Session::poll_events`] handles events, and the
    /// window redraws continuously while the animation runs.
    ///
    /// ```no_run
    /// # use spectra_plot::*;
    /// # fn main() -> Result<()> {
    /// let session = Session::new()?;
    /// let fig = session.figure("wave")?;
    /// let x = linspace(0.0, 10.0, 200);
    /// let line = fig.subplot(1, 1, 1)?.line(&x, &x)?;
    /// fig.animate(60.0, 0.0, move |f| {
    ///     let y: Vec<f64> = x.iter().map(|v| (v + f.t as f64).sin()).collect();
    ///     let _ = line.set_data(&x, &y);
    /// })?;
    /// session.show()?;
    /// # Ok(()) }
    /// ```
    pub fn animate<F>(&self, fps: f32, duration: f32, on_frame: F) -> Result<Animator>
    where
        F: FnMut(AnimFrame) + Send + 'static,
    {
        if !(fps > 0.0 && fps.is_finite()) || duration.is_nan() || duration < 0.0 {
            return Err(Error::invalid("animate needs fps > 0 and duration >= 0"));
        }
        self.session.request(
            p::REQ_ANIM_START,
            &codec::encode_anim_start(self.state.id, fps, duration),
        )?;
        let running = Arc::new(AtomicBool::new(true));
        let now = Instant::now();
        self.session.register_animator(
            self.state.id,
            AnimEntry {
                on_frame: Box::new(on_frame),
                interval: Duration::from_secs_f64(1.0 / fps as f64),
                duration,
                start: now,
                last: now,
                next: now,
                frame: 0,
                running: running.clone(),
            },
        );
        Ok(Animator::new(self.clone(), running))
    }
}
