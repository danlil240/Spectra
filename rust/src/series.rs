//! Series: one plotted data set (line, scatter, bars, surface, ...).

use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};
use std::sync::{Arc, Mutex};

use crate::axes::Axes;
use crate::codec::{self, PropertyUpdate};
use crate::color::{Color, ColorSpec, IntoColor};
use crate::data::{self, Values};
use crate::error::{Error, Result};
use crate::protocol as p;
use crate::session::lock;
use crate::style::{DenseMode, DrawStyle, Format, LineStyle, Marker};

/// What kind of series this is (the backend's series type).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum SeriesKind {
    /// Connected 2D line.
    Line,
    /// 2D markers.
    Scatter,
    /// Bars (vertical or horizontal).
    Bar,
    /// Histogram (bins computed by the renderer).
    Histogram,
    /// Filled band between two curves.
    Band,
    /// Stems with heads.
    Stem,
    /// Box-and-whisker plot.
    Box,
    /// Violin plot.
    Violin,
    /// 3D line.
    Line3d,
    /// 3D markers.
    Scatter3d,
    /// 3D surface.
    Surface,
    /// Grid drawn as an image: imshow, heatmap or filled contours.
    Image,
    /// Contour lines of a grid.
    Contour,
}

impl SeriesKind {
    /// Backend series type name.
    pub fn wire_name(self) -> &'static str {
        match self {
            SeriesKind::Line => "line",
            SeriesKind::Scatter => "scatter",
            SeriesKind::Bar => "bar",
            SeriesKind::Histogram => "histogram",
            SeriesKind::Band => "band",
            SeriesKind::Stem => "stem",
            SeriesKind::Box => "box",
            SeriesKind::Violin => "violin",
            SeriesKind::Line3d => "line3d",
            SeriesKind::Scatter3d => "scatter3d",
            SeriesKind::Surface => "surface",
            SeriesKind::Image => "image",
            SeriesKind::Contour => "contour",
        }
    }

    /// Whether the series lives in a 3D axes.
    pub fn is_3d(self) -> bool {
        matches!(
            self,
            SeriesKind::Line3d | SeriesKind::Scatter3d | SeriesKind::Surface
        )
    }

    /// Whether the data is a structured statistics payload (not x/y points).
    pub fn is_statistical(self) -> bool {
        matches!(
            self,
            SeriesKind::Bar
                | SeriesKind::Histogram
                | SeriesKind::Band
                | SeriesKind::Stem
                | SeriesKind::Box
                | SeriesKind::Violin
                | SeriesKind::Image
                | SeriesKind::Contour
        )
    }
}

pub(crate) struct SeriesState {
    /// Figure-wide index; shifts down when an earlier series is removed.
    pub index: AtomicU32,
    pub kind: SeriesKind,
    pub label: Mutex<String>,
    /// Double-precision (x, y) origin; values are sent as f32 offsets from it.
    pub origin: Mutex<(f64, f64)>,
    pub has_data: AtomicBool,
    pub draw: Mutex<(DrawStyle, DenseMode)>,
    pub removed: AtomicBool,
    pub topic: Mutex<Option<String>>,
}

impl SeriesState {
    pub fn new(index: u32, kind: SeriesKind, label: &str) -> Self {
        SeriesState {
            index: AtomicU32::new(index),
            kind,
            label: Mutex::new(label.to_string()),
            origin: Mutex::new((0.0, 0.0)),
            has_data: AtomicBool::new(false),
            draw: Mutex::new((DrawStyle::Default, DenseMode::Auto)),
            removed: AtomicBool::new(false),
            topic: Mutex::new(None),
        }
    }
}

/// A data series in an axes. Cheap to clone; clones refer to the same series.
///
/// Setters return `Result<&Self>` so they chain:
///
/// ```no_run
/// # use spectra_plot::*;
/// # fn f(ax: &Axes) -> Result<()> {
/// ax.line(&[0.0, 1.0, 2.0], &[1.0, 3.0, 2.0])?
///     .set_label("measured")?
///     .set_color("tab:orange")?
///     .set_line_width(2.5)?;
/// # Ok(()) }
/// ```
#[derive(Clone)]
pub struct Series {
    pub(crate) axes: Axes,
    pub(crate) state: Arc<SeriesState>,
}

impl std::fmt::Debug for Series {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Series")
            .field("figure_id", &self.axes.figure_id())
            .field("index", &self.index())
            .field("kind", &self.kind())
            .field("label", &self.label())
            .finish()
    }
}

impl PartialEq for Series {
    fn eq(&self, other: &Self) -> bool {
        Arc::ptr_eq(&self.state, &other.state)
    }
}

impl Series {
    /// Figure-wide series index used by the backend.
    pub fn index(&self) -> u32 {
        self.state.index.load(Ordering::SeqCst)
    }

    /// The series type.
    pub fn kind(&self) -> SeriesKind {
        self.state.kind
    }

    /// Legend label.
    pub fn label(&self) -> String {
        lock(&self.state.label).clone()
    }

    /// The axes holding this series.
    pub fn axes(&self) -> &Axes {
        &self.axes
    }

    fn check_alive(&self) -> Result<()> {
        if self.state.removed.load(Ordering::SeqCst) {
            Err(Error::invalid("this series was removed"))
        } else {
            Ok(())
        }
    }

    fn prop(&self, name: &str) -> PropertyUpdate {
        PropertyUpdate::new(self.axes.figure_id(), name).series(self.index())
    }

    fn send(&self, u: PropertyUpdate) -> Result<&Self> {
        self.check_alive()?;
        self.axes.session().update(u)?;
        Ok(self)
    }

    // ─── Data ───────────────────────────────────────────────────────────

    /// Replace the x/y data of a 2D line or scatter series.
    ///
    /// Values too large for `f32` precision (e.g. Unix timestamps) are sent as
    /// offsets from a double-precision origin that the backend keeps.
    pub fn set_data(
        &self,
        x: &(impl Values + ?Sized),
        y: &(impl Values + ?Sized),
    ) -> Result<&Self> {
        let (x, y) = (x.as_f64(), y.as_f64());
        self.set_data_f64(&x, &y)?;
        self.axes.fit_points(&x, &y)?;
        Ok(self)
    }

    pub(crate) fn set_data_f64(&self, x: &[f64], y: &[f64]) -> Result<()> {
        self.check_alive()?;
        let kind = self.kind();
        if kind.is_3d() || kind.is_statistical() {
            return Err(Error::invalid(format!(
                "set_data(x, y) is for line and scatter series; use the {} series' own call",
                kind.wire_name()
            )));
        }
        if x.len() != y.len() {
            return Err(Error::invalid(format!(
                "x and y must have the same length ({} vs {})",
                x.len(),
                y.len()
            )));
        }
        let origin = (data::choose_data_origin(x), data::choose_data_origin(y));
        let flat = data::interleave(&[(x, origin.0), (y, origin.1)]);
        self.send_floats(&flat)?;
        // The backend resets the origin on new data: send it afterwards.
        if origin != (0.0, 0.0) {
            self.send(self.prop("data_origin").f2(origin.0, origin.1))?;
        }
        *lock(&self.state.origin) = origin;
        self.state.has_data.store(!x.is_empty(), Ordering::SeqCst);
        Ok(())
    }

    /// Send a full float payload, chunked when it exceeds the protocol limit.
    pub(crate) fn send_floats(&self, flat: &[f32]) -> Result<()> {
        let session = self.axes.session();
        let fid = self.axes.figure_id();
        let idx = self.index();
        let floats_per_chunk = p::CHUNK_SIZE / 4;
        if flat.len() <= floats_per_chunk {
            session.request(p::REQ_SET_DATA, &codec::encode_set_data(fid, idx, flat))?;
            return Ok(());
        }
        let chunks: Vec<&[f32]> = flat.chunks(floats_per_chunk).collect();
        let total = u32::try_from(flat.len())
            .map_err(|_| Error::invalid("data has more than 4 billion values"))?;
        for (i, c) in chunks.iter().enumerate() {
            let payload =
                codec::encode_set_data_chunk(fid, idx, c, i as u32, chunks.len() as u32, total);
            session.request(p::REQ_SET_DATA, &payload)?;
        }
        Ok(())
    }

    /// Append x/y points to a 2D line or scatter series (streaming).
    ///
    /// Automatic axis limits grow to include the new points.
    pub fn append(&self, x: &(impl Values + ?Sized), y: &(impl Values + ?Sized)) -> Result<&Self> {
        self.check_alive()?;
        let kind = self.kind();
        if kind.is_3d() || kind.is_statistical() {
            return Err(Error::invalid(format!(
                "append(x, y) is not supported for {} series",
                kind.wire_name()
            )));
        }
        let (x, y) = (x.as_f64(), y.as_f64());
        if x.len() != y.len() {
            return Err(Error::invalid(format!(
                "x and y must have the same length ({} vs {})",
                x.len(),
                y.len()
            )));
        }
        if x.is_empty() {
            return Ok(self);
        }
        if !self.state.has_data.load(Ordering::SeqCst) {
            let origin = (
                data::choose_stream_origin(x[0]),
                data::choose_stream_origin(y[0]),
            );
            if origin != (0.0, 0.0) {
                self.send(self.prop("data_origin").f2(origin.0, origin.1))?;
            }
            *lock(&self.state.origin) = origin;
            self.state.has_data.store(true, Ordering::SeqCst);
        }
        let origin = *lock(&self.state.origin);
        let flat = data::interleave(&[(&x, origin.0), (&y, origin.1)]);
        self.axes.session().request(
            p::REQ_APPEND_DATA,
            &codec::encode_append_data(self.axes.figure_id(), self.index(), &flat),
        )?;
        self.axes.fit_points(&x, &y)?;
        Ok(self)
    }

    /// Replace the x/y/z data of a 3D line or scatter series.
    pub fn set_data_xyz(
        &self,
        x: &(impl Values + ?Sized),
        y: &(impl Values + ?Sized),
        z: &(impl Values + ?Sized),
    ) -> Result<&Self> {
        self.check_alive()?;
        if !self.kind().is_3d() {
            return Err(Error::invalid("set_data_xyz is for 3D series"));
        }
        let (x, y, z) = (x.as_f64(), y.as_f64(), z.as_f64());
        if x.len() != y.len() || x.len() != z.len() {
            return Err(Error::invalid(format!(
                "x, y and z must have the same length ({}, {}, {})",
                x.len(),
                y.len(),
                z.len()
            )));
        }
        let flat = data::interleave(&[(&x, 0.0), (&y, 0.0), (&z, 0.0)]);
        self.send_floats(&flat)?;
        self.state.has_data.store(!x.is_empty(), Ordering::SeqCst);
        self.axes.fit_points3(&x, &y, &z)?;
        Ok(self)
    }

    /// Append x/y/z points to a 3D line or scatter series.
    pub fn append_xyz(
        &self,
        x: &(impl Values + ?Sized),
        y: &(impl Values + ?Sized),
        z: &(impl Values + ?Sized),
    ) -> Result<&Self> {
        self.check_alive()?;
        if !matches!(self.kind(), SeriesKind::Line3d | SeriesKind::Scatter3d) {
            return Err(Error::invalid(
                "append_xyz is for 3D line and scatter series",
            ));
        }
        let (x, y, z) = (x.as_f64(), y.as_f64(), z.as_f64());
        if x.len() != y.len() || x.len() != z.len() {
            return Err(Error::invalid("x, y and z must have the same length"));
        }
        let flat = data::interleave(&[(&x, 0.0), (&y, 0.0), (&z, 0.0)]);
        self.axes.session().request(
            p::REQ_APPEND_DATA,
            &codec::encode_append_data(self.axes.figure_id(), self.index(), &flat),
        )?;
        self.state.has_data.store(true, Ordering::SeqCst);
        self.axes.fit_points3(&x, &y, &z)?;
        Ok(self)
    }

    pub(crate) fn set_payload(&self, data: &[f32]) -> Result<()> {
        self.send_floats(data)?;
        self.state
            .has_data
            .store(!data.is_empty(), Ordering::SeqCst);
        Ok(())
    }

    pub(crate) fn set_categories(&self, names: &[&str]) -> Result<()> {
        let joined = names
            .iter()
            .map(|n| n.replace(p::CATEGORY_SEPARATOR, " "))
            .collect::<Vec<_>>()
            .join(&p::CATEGORY_SEPARATOR.to_string());
        self.send(self.prop("categories").f(names.len() as f64).s(joined))?;
        Ok(())
    }

    // ─── Appearance ─────────────────────────────────────────────────────

    /// Set the legend label.
    pub fn set_label(&self, label: &str) -> Result<&Self> {
        self.send(self.prop("label").s(label))?;
        *lock(&self.state.label) = label.to_string();
        Ok(self)
    }

    /// Colour the series: a name (`"r"`, `"steelblue"`, `"#ff8800"`, `"C2"`,
    /// `"tab:blue"`), a [`Color`], a [`ColorSpec`] or an RGB(A) tuple/array.
    ///
    /// Letters and `"C<n>"` palette slots follow the window's theme.
    pub fn set_color(&self, color: impl IntoColor) -> Result<&Self> {
        match color.into_color_spec()? {
            ColorSpec::Letter(c) => self.send(self.prop("color_letter").s(c.to_string())),
            ColorSpec::Palette(n) => self.send(self.prop("palette_index").f(n as f64)),
            ColorSpec::Rgba(c) => self.send(
                self.prop("color")
                    .f4(c.r as f64, c.g as f64, c.b as f64, c.a as f64),
            ),
        }
    }

    /// Colour from RGBA components in `0.0..=1.0`.
    pub fn set_color_rgba(&self, r: f32, g: f32, b: f32, a: f32) -> Result<&Self> {
        self.set_color(Color::rgba(r, g, b, a))
    }

    /// Line width in pixels.
    pub fn set_line_width(&self, width: f32) -> Result<&Self> {
        if !(width >= 0.0 && width.is_finite()) {
            return Err(Error::invalid("line width must be >= 0"));
        }
        self.send(self.prop("line_width").f(width as f64))
    }

    /// Marker size in pixels.
    pub fn set_marker_size(&self, size: f32) -> Result<&Self> {
        if !(size >= 0.0 && size.is_finite()) {
            return Err(Error::invalid("marker size must be >= 0"));
        }
        self.send(self.prop("marker_size").f(size as f64))
    }

    /// Line dash style.
    pub fn set_line_style(&self, style: LineStyle) -> Result<&Self> {
        self.send(self.prop("line_style").s(style.token()))
    }

    /// Marker shape.
    pub fn set_marker(&self, marker: Marker) -> Result<&Self> {
        self.send(self.prop("marker_style").s(marker.token()))
    }

    /// Apply a MATLAB / matplotlib format string such as `"r--o"` or `"g^"`.
    pub fn set_format(&self, fmt: &str) -> Result<&Self> {
        let f = Format::parse(fmt)?;
        self.apply_format(&f)
    }

    pub(crate) fn apply_format(&self, f: &Format) -> Result<&Self> {
        let mut ups = Vec::new();
        if let Some(c) = f.color {
            ups.push(self.prop("color_letter").s(c.to_string()));
        }
        if let Some(l) = f.line {
            ups.push(self.prop("line_style").s(l.token()));
        }
        if let Some(m) = f.marker {
            ups.push(self.prop("marker_style").s(m.token()));
        }
        self.check_alive()?;
        self.axes.session().update_batch(&ups)?;
        Ok(self)
    }

    /// How a line connects its points (steps), line series only.
    pub fn set_draw_style(&self, style: DrawStyle) -> Result<&Self> {
        lock(&self.state.draw).0 = style;
        self.send_draw()
    }

    /// How a line with far more points than pixels is drawn, line series only.
    pub fn set_dense_mode(&self, mode: DenseMode) -> Result<&Self> {
        lock(&self.state.draw).1 = mode;
        self.send_draw()
    }

    fn send_draw(&self) -> Result<&Self> {
        if self.kind() != SeriesKind::Line {
            return Err(Error::invalid("draw style applies to line series only"));
        }
        let (d, m) = *lock(&self.state.draw);
        self.send(self.prop("draw_style").f2(d.wire() as f64, m.wire() as f64))
    }

    /// Show or hide the series.
    pub fn set_visible(&self, visible: bool) -> Result<&Self> {
        self.send(self.prop("visible").b(visible))
    }

    /// Overall opacity, `0.0..=1.0`.
    pub fn set_opacity(&self, opacity: f32) -> Result<&Self> {
        if !(0.0..=1.0).contains(&opacity) {
            return Err(Error::invalid("opacity must be in [0, 1]"));
        }
        self.send(self.prop("opacity").f(opacity as f64))
    }

    /// 3D scatter / line: fade parts farther from the camera (on by default for 3D scatter).
    pub fn set_depth_shade(&self, enabled: bool) -> Result<&Self> {
        self.send(self.prop("depth_shade").b(enabled))
    }

    /// Surface: draw the grid edge lines over the faces, optionally in `color`.
    pub fn set_show_edges(&self, enabled: bool, color: Option<Color>) -> Result<&Self> {
        let mut u = self.prop("show_edges").b(enabled);
        if let Some(c) = color {
            let c = match c.into_color_spec()? {
                ColorSpec::Rgba(c) => c,
                other => other.to_color(),
            };
            u = u.data(c.to_array().to_vec());
        }
        self.send(u)
    }

    /// Surface: project `levels` contour lines onto the floor of the 3D box (MATLAB `surfc`).
    pub fn set_contour_projection(&self, enabled: bool, levels: u32) -> Result<&Self> {
        if enabled && levels < 1 {
            return Err(Error::invalid("levels must be at least 1"));
        }
        self.send(self.prop("contour_projection").f(levels as f64).b(enabled))
    }

    /// Surface, image, contour or colormapped scatter: colour by value through a named
    /// colormap (`"viridis"`, `"plasma"`, `"turbo"`, `"RdBu"`, ...; `"_r"`
    /// reverses; `"none"` turns colormapping off).
    pub fn set_colormap(&self, name: &str) -> Result<&Self> {
        if name.trim().is_empty() {
            return Err(Error::invalid("colormap name must not be empty"));
        }
        self.send(self.prop("colormap").s(name))
    }

    /// Colormap from 1 to 256 colours spread evenly over the value range.
    pub fn set_colormap_colors<C: IntoColor + Clone>(&self, colors: &[C]) -> Result<&Self> {
        if colors.is_empty() || colors.len() > 256 {
            return Err(Error::invalid(
                "a colour list colormap needs 1 to 256 colours",
            ));
        }
        let mut data = Vec::with_capacity(colors.len() * 4);
        for c in colors {
            data.extend_from_slice(&c.clone().into_color_spec()?.to_color().to_array());
        }
        self.send(self.prop("colormap").s("custom").data(data))
    }

    /// Scatter (2D / 3D): one value per point, coloured through the series
    /// colormap (see [`Series::set_colormap`]); an empty slice clears them.
    pub fn set_color_values(&self, values: &[f64]) -> Result<&Self> {
        let data = values.iter().map(|&v| v as f32).collect();
        self.send(self.prop("color_values").data(data))
    }

    /// Values mapped onto the ends of the colormap (`None` = the data range).
    pub fn set_colormap_range(&self, range: Option<(f64, f64)>) -> Result<&Self> {
        match range {
            None => self.send(self.prop("colormap_range").f2(0.0, 0.0).b(false)),
            Some((lo, hi)) => {
                if !(lo.is_finite() && hi.is_finite() && lo < hi) {
                    return Err(Error::invalid("colormap range needs finite vmin < vmax"));
                }
                self.send(self.prop("colormap_range").f2(lo, hi).b(true))
            }
        }
    }

    /// Remove the series from its axes.
    pub fn remove(&self) -> Result<()> {
        self.check_alive()?;
        let idx = self.index();
        self.axes.session().request(
            p::REQ_REMOVE_SERIES,
            &codec::encode_remove_series(self.axes.figure_id(), idx),
        )?;
        self.axes.on_series_removed(idx);
        Ok(())
    }

    /// The topic this series is subscribed to, if any (see [`Axes::subscribe_topic`]).
    pub fn topic(&self) -> Option<String> {
        lock(&self.state.topic).clone()
    }

    /// Stop feeding this series from its topic (the data drawn so far stays).
    pub fn unsubscribe_topic(&self) -> Result<&Self> {
        self.check_alive()?;
        if lock(&self.state.topic).is_none() {
            return Err(Error::invalid("this series is not subscribed to a topic"));
        }
        self.axes.session().request(
            p::REQ_UNSUBSCRIBE_TOPIC,
            &codec::encode_unsubscribe_topic(
                self.axes.figure_id(),
                self.axes.index(),
                self.index(),
            ),
        )?;
        *lock(&self.state.topic) = None;
        Ok(self)
    }
}
