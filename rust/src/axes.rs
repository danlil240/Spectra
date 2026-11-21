//! Axes: one subplot (2D or 3D) holding series and annotations.

use std::sync::atomic::Ordering;
use std::sync::{Arc, Mutex, Weak};

use crate::annotation::{AnnotationState, TextAnnotation, TextOptions};
use crate::codec::{self, PropertyUpdate};
use crate::color::{Color, IntoColor};
use crate::data::{self, Values};
use crate::error::{Error, Result};
use crate::figure::Figure;
use crate::protocol as p;
use crate::series::{Series, SeriesKind, SeriesState};
use crate::session::{lock, Session};
use crate::style::{BarMode, DrawStyle, Format, LightMode, Scale, Share};

/// Span of [`Axes::hline`] / [`Axes::vline`] (excluded from automatic limits).
const REF_SPAN: f64 = 1e12;

// ─── Automatic limits ────────────────────────────────────────────────────────

#[derive(Clone)]
struct BarRec {
    pos: Vec<f64>,
    heights: Vec<f64>,
    bottom: f64,
    horizontal: bool,
    width: f64,
}

/// Client-side automatic limits: the backend draws exactly the limits it is
/// given, so the client fits them to the data (like the Python easy API).
#[derive(Clone)]
struct Fit {
    /// Automatic limits on for x, y, z (turned off by an explicit `set_*lim`).
    auto: [bool; 3],
    /// Accumulated data bounds per axis.
    bounds: [Option<(f64, f64)>; 3],
    /// Smallest positive value per axis (for log scales).
    pos: [Option<f64>; 2],
    /// Sticky edges (bar baselines) per axis: padding never crosses them.
    sticky: [Option<(f64, f64)>; 2],
    scales: [Scale; 2],
    bars: Vec<BarRec>,
    /// Limits last sent, to skip redundant updates.
    sent: [Option<(f64, f64)>; 3],
}

impl Default for Fit {
    fn default() -> Self {
        Fit {
            auto: [true; 3],
            bounds: [None; 3],
            pos: [None; 2],
            sticky: [None; 2],
            scales: [Scale::Linear; 2],
            bars: Vec::new(),
            sent: [None; 3],
        }
    }
}

fn merge(a: Option<(f64, f64)>, lo: f64, hi: f64) -> (f64, f64) {
    match a {
        Some((l, h)) => (l.min(lo), h.max(hi)),
        None => (lo, hi),
    }
}

fn min_opt(a: Option<f64>, b: Option<f64>) -> Option<f64> {
    match (a, b) {
        (Some(x), Some(y)) => Some(x.min(y)),
        (x, None) => x,
        (None, y) => y,
    }
}

/// Padded limits for data spanning `[lo, hi]` on a `scale` axis.
fn fit_range(lo: f64, hi: f64, pos: Option<f64>, scale: Scale) -> Option<(f64, f64)> {
    match scale {
        Scale::Log | Scale::Log2 => {
            let lo = pos?;
            if hi <= 0.0 {
                return None;
            }
            let span = hi.log10() - lo.log10();
            let f = if span > 0.0 {
                10f64.powf(span * 0.05)
            } else {
                2.0
            };
            Some((lo / f, hi * f))
        }
        _ => {
            let pad = if hi != lo { (hi - lo) * 0.05 } else { 0.5 };
            let mut out = (lo - pad, hi + pad);
            if scale == Scale::Sqrt {
                out.0 = out.0.max(0.0);
            }
            Some(out)
        }
    }
}

/// Stop linear padding at a sticky edge that bounds the data (bars stand on their baseline).
fn stick(
    rng: (f64, f64),
    lo: f64,
    hi: f64,
    sticky: Option<(f64, f64)>,
    scale: Scale,
) -> (f64, f64) {
    let Some((s_lo, s_hi)) = sticky else {
        return rng;
    };
    if scale != Scale::Linear {
        return rng;
    }
    let (mut r_lo, mut r_hi) = rng;
    if lo >= s_lo && r_lo < s_lo {
        r_lo = s_lo;
    }
    if hi <= s_hi && r_hi > s_hi && s_hi > r_lo {
        r_hi = s_hi;
    }
    (r_lo, r_hi)
}

/// Values the bars of one orientation must show, and their baselines
/// (mirrors `Axes::sync_bar_layout()`: stacked bars stack from the first
/// series' bottom, positives and negatives separately).
fn bar_extent(bars: &[&BarRec], mode: BarMode) -> (Vec<f64>, Vec<f64>) {
    if mode != BarMode::Stacked || bars.len() < 2 {
        let mut ext: Vec<f64> = bars
            .iter()
            .flat_map(|b| b.heights.iter().copied())
            .collect();
        ext.extend(bars.iter().map(|b| b.bottom));
        let mut origins: Vec<f64> = bars.iter().map(|b| b.bottom).collect();
        origins.sort_by(f64::total_cmp);
        origins.dedup();
        return (ext, origins);
    }
    let origin = bars[0].bottom;
    let mut tops: Vec<(f64, f64, f64)> = Vec::new(); // (position, positive top, negative top)
    let mut ext = vec![origin];
    for b in bars {
        for (&p, &h) in b.pos.iter().zip(&b.heights) {
            if !(p.is_finite() && h.is_finite()) {
                continue;
            }
            let slot = match tops.iter().position(|t| t.0 == p) {
                Some(i) => i,
                None => {
                    tops.push((p, origin, origin));
                    tops.len() - 1
                }
            };
            if h >= 0.0 {
                tops[slot].1 += h;
                ext.push(tops[slot].1);
            } else {
                tops[slot].2 += h;
                ext.push(tops[slot].2);
            }
        }
    }
    (ext, vec![origin])
}

pub(crate) struct ShareGroup {
    members: Mutex<Vec<Weak<AxesState>>>,
}

pub(crate) struct AxesState {
    pub figure_id: u64,
    pub index: u32,
    pub is_3d: bool,
    pub series: Mutex<Vec<Arc<SeriesState>>>,
    pub annotations: Mutex<Vec<Arc<Mutex<AnnotationState>>>>,
    pub bar_mode: Mutex<BarMode>,
    fit: Mutex<Fit>,
    share: Mutex<[Option<Arc<ShareGroup>>; 2]>,
    /// twinx(): index of the host axes, -1 for an ordinary axes.
    pub twin_of: i32,
    /// twinx(): the twin of this axes, once made.
    pub twin: Mutex<Option<Arc<AxesState>>>,
}

impl AxesState {
    pub fn new(figure_id: u64, index: u32, is_3d: bool) -> Self {
        AxesState {
            figure_id,
            index,
            is_3d,
            series: Mutex::new(Vec::new()),
            annotations: Mutex::new(Vec::new()),
            bar_mode: Mutex::new(BarMode::Grouped),
            fit: Mutex::new(Fit::default()),
            share: Mutex::new([None, None]),
            twin_of: -1,
            twin: Mutex::new(None),
        }
    }

    fn group(self: &Arc<Self>, axis: usize) -> Vec<Arc<AxesState>> {
        let g = lock(&self.share)[axis].clone();
        match g {
            Some(g) => lock(&g.members).iter().filter_map(Weak::upgrade).collect(),
            None => vec![self.clone()],
        }
    }
}

/// Link the limits of a subplot grid (row-major) like `subplots(sharex=...)`.
pub(crate) fn link_shared(axes: &[Axes], cols: usize, sharex: Share, sharey: Share) {
    for (axis, mode) in [(0usize, sharex), (1usize, sharey)] {
        let mut groups: Vec<(usize, Arc<ShareGroup>)> = Vec::new();
        for (i, ax) in axes.iter().enumerate() {
            let part = match mode {
                Share::None => {
                    lock(&ax.state.share)[axis] = None;
                    continue;
                }
                Share::All => 0,
                Share::Row => i / cols,
                Share::Col => i % cols,
            };
            let g = match groups.iter().find(|(k, _)| *k == part) {
                Some((_, g)) => g.clone(),
                None => {
                    let g = Arc::new(ShareGroup {
                        members: Mutex::new(Vec::new()),
                    });
                    groups.push((part, g.clone()));
                    g
                }
            };
            lock(&g.members).push(Arc::downgrade(&ax.state));
            lock(&ax.state.share)[axis] = Some(g);
        }
    }
    for ax in axes {
        let _ = ax.apply_fit();
    }
}

// ─── Options ─────────────────────────────────────────────────────────────────

/// Fill styling shared by bars, histograms, box and violin plots.
#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub struct FillStyle {
    /// Fill opacity (default 0.88).
    pub fill_alpha: Option<f32>,
    /// Edge width in pixels (default 1).
    pub edge_width: Option<f32>,
    /// Edge colour (default: a theme-aware shade of the fill).
    pub edge_color: Option<Color>,
}

impl FillStyle {
    fn trailer(&self) -> Result<Vec<f32>> {
        if self.fill_alpha.is_none() && self.edge_width.is_none() && self.edge_color.is_none() {
            return Ok(Vec::new());
        }
        let a = self.fill_alpha.unwrap_or(0.88);
        let w = self.edge_width.unwrap_or(1.0);
        if !(a.is_finite() && (0.0..=1.0).contains(&a)) {
            return Err(Error::invalid(format!(
                "fill_alpha must be in [0, 1], got {a}"
            )));
        }
        if !(w.is_finite() && w >= 0.0) {
            return Err(Error::invalid(format!("edge_width must be >= 0, got {w}")));
        }
        let c = match self.edge_color {
            Some(c) => c.into_color_spec()?.to_color().to_array(),
            None => [0.0, 0.0, 0.0, 1.0],
        };
        let mut v = vec![
            p::STAT_STYLE_TAG,
            a,
            w,
            if self.edge_color.is_some() { 1.0 } else { 0.0 },
        ];
        v.extend_from_slice(&c);
        Ok(v)
    }
}

/// Options for [`Axes::bar_with`].
#[derive(Debug, Clone, PartialEq)]
pub struct BarOptions {
    /// Bar width in data units (default 0.8).
    pub width: f64,
    /// Baseline the bars stand on (default 0).
    pub bottom: f64,
    /// Bars along x instead of y.
    pub horizontal: bool,
    /// Legend label.
    pub label: String,
    /// Fill styling.
    pub fill: FillStyle,
}

impl Default for BarOptions {
    fn default() -> Self {
        BarOptions {
            width: 0.8,
            bottom: 0.0,
            horizontal: false,
            label: String::new(),
            fill: FillStyle::default(),
        }
    }
}

/// Options for [`Axes::hist_with`].
#[derive(Debug, Clone, PartialEq)]
pub struct HistOptions {
    /// Number of equal-width bins (default 30).
    pub bins: u32,
    /// Normalise to a probability density.
    pub density: bool,
    /// Accumulate the bins (a CDF with `density`).
    pub cumulative: bool,
    /// Legend label.
    pub label: String,
    /// Fill styling.
    pub fill: FillStyle,
}

impl Default for HistOptions {
    fn default() -> Self {
        HistOptions {
            bins: 30,
            density: false,
            cumulative: false,
            label: String::new(),
            fill: FillStyle::default(),
        }
    }
}

/// Options for [`Axes::fill_between_with`].
#[derive(Debug, Clone, PartialEq)]
pub struct BandOptions {
    /// Fill opacity (default 0.25).
    pub alpha: f32,
    /// Edge line width (default 1).
    pub edge_width: f32,
    /// Draw the edge lines (default `true`).
    pub edges: bool,
    /// Legend label.
    pub label: String,
}

impl Default for BandOptions {
    fn default() -> Self {
        BandOptions {
            alpha: 0.25,
            edge_width: 1.0,
            edges: true,
            label: String::new(),
        }
    }
}

/// Options for [`Axes::stem_with`].
#[derive(Debug, Clone, PartialEq)]
pub struct StemOptions {
    /// Value the stems start from (default 0).
    pub baseline: f64,
    /// Stem line width (default 1.5).
    pub stem_width: f32,
    /// Draw round heads (default `true`).
    pub heads: bool,
    /// Legend label.
    pub label: String,
}

impl Default for StemOptions {
    fn default() -> Self {
        StemOptions {
            baseline: 0.0,
            stem_width: 1.5,
            heads: true,
            label: String::new(),
        }
    }
}

/// Options for [`Axes::boxplot_with`].
#[derive(Debug, Clone, PartialEq)]
pub struct BoxOptions {
    /// Box centres (default 1, 2, 3, ...).
    pub positions: Option<Vec<f64>>,
    /// Box width (default 0.6).
    pub width: f32,
    /// Draw points beyond the whiskers (default `true`).
    pub outliers: bool,
    /// Notch the boxes around the median.
    pub notched: bool,
    /// Legend label.
    pub label: String,
    /// Fill styling.
    pub fill: FillStyle,
}

impl Default for BoxOptions {
    fn default() -> Self {
        BoxOptions {
            positions: None,
            width: 0.6,
            outliers: true,
            notched: false,
            label: String::new(),
            fill: FillStyle::default(),
        }
    }
}

/// Options for [`Axes::violin_with`].
#[derive(Debug, Clone, PartialEq)]
pub struct ViolinOptions {
    /// Violin centres (default 1, 2, 3, ...).
    pub positions: Option<Vec<f64>>,
    /// Violin width (default 0.8).
    pub width: f32,
    /// Density samples along each violin (default 50).
    pub resolution: u32,
    /// Draw a box plot inside (default `true`).
    pub show_box: bool,
    /// Legend label.
    pub label: String,
    /// Fill styling.
    pub fill: FillStyle,
}

impl Default for ViolinOptions {
    fn default() -> Self {
        ViolinOptions {
            positions: None,
            width: 0.8,
            resolution: 50,
            show_box: true,
            label: String::new(),
            fill: FillStyle::default(),
        }
    }
}

/// Error bar sizes: the same value everywhere, one per point, or separate
/// lower and upper values per point.
#[derive(Debug, Clone, PartialEq)]
pub enum ErrorSpec {
    /// The same error for every point.
    Scalar(f64),
    /// One symmetric error per point.
    Symmetric(Vec<f64>),
    /// `(lower, upper)` errors per point.
    Asymmetric(Vec<f64>, Vec<f64>),
}

impl ErrorSpec {
    fn expand(&self, n: usize, name: &str) -> Result<(Vec<f64>, Vec<f64>)> {
        let (lo, hi) = match self {
            ErrorSpec::Scalar(v) => (vec![v.abs(); n], vec![v.abs(); n]),
            ErrorSpec::Symmetric(v) => (v.clone(), v.clone()),
            ErrorSpec::Asymmetric(l, h) => (l.clone(), h.clone()),
        };
        if lo.len() != n || hi.len() != n {
            return Err(Error::invalid(format!(
                "{name} has {} values but the data has {n}",
                lo.len().max(hi.len())
            )));
        }
        Ok((
            lo.iter().map(|v| v.abs()).collect(),
            hi.iter().map(|v| v.abs()).collect(),
        ))
    }
}

/// How [`Axes::errorbar_with`] draws the data points.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum ErrorBarStyle {
    /// A line through the points (default).
    #[default]
    Line,
    /// Markers at the points.
    Markers,
    /// Only the error bars.
    None,
}

/// Options for [`Axes::errorbar_with`].
#[derive(Debug, Clone, PartialEq, Default)]
pub struct ErrorBarOptions {
    /// Vertical errors.
    pub yerr: Option<ErrorSpec>,
    /// Horizontal errors.
    pub xerr: Option<ErrorSpec>,
    /// How the data points are drawn.
    pub style: ErrorBarStyle,
    /// Full cap width in data units (0 = no caps).
    pub capsize: f64,
    /// Colour of points and bars (default: a theme letter per call).
    pub color: Option<String>,
    /// Legend label.
    pub label: String,
}

/// The series created by [`Axes::errorbar_with`].
#[derive(Debug, Clone)]
pub struct ErrorBar {
    /// Line or markers through the points (`None` with [`ErrorBarStyle::None`]).
    pub data: Option<Series>,
    /// The error bars.
    pub bars: Option<Series>,
}

// ─── Axes ────────────────────────────────────────────────────────────────────

/// One subplot. Cheap to clone; clones refer to the same axes.
#[derive(Clone)]
pub struct Axes {
    pub(crate) figure: Figure,
    pub(crate) state: Arc<AxesState>,
}

impl std::fmt::Debug for Axes {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Axes")
            .field("figure_id", &self.figure_id())
            .field("index", &self.index())
            .field("is_3d", &self.is_3d())
            .finish()
    }
}

impl PartialEq for Axes {
    fn eq(&self, other: &Self) -> bool {
        Arc::ptr_eq(&self.state, &other.state)
    }
}

/// Matplotlib-style errorbar colours when none is given (theme-aware letters).
const ERRORBAR_CYCLE: [&str; 6] = ["b", "r", "g", "m", "c", "y"];

impl Axes {
    pub(crate) fn from_state(figure: Figure, state: Arc<AxesState>) -> Self {
        Axes { figure, state }
    }

    /// Axes index within the figure.
    pub fn index(&self) -> u32 {
        self.state.index
    }

    /// Id of the owning figure.
    pub fn figure_id(&self) -> u64 {
        self.state.figure_id
    }

    /// The owning figure.
    pub fn figure(&self) -> &Figure {
        &self.figure
    }

    /// The session.
    pub fn session(&self) -> &Session {
        &self.figure.session
    }

    /// Whether this is a 3D axes.
    pub fn is_3d(&self) -> bool {
        self.state.is_3d
    }

    /// Series in this axes, in creation order.
    pub fn series(&self) -> Vec<Series> {
        lock(&self.state.series)
            .iter()
            .map(|s| Series {
                axes: self.clone(),
                state: s.clone(),
            })
            .collect()
    }

    fn prop(&self, name: &str) -> PropertyUpdate {
        PropertyUpdate::new(self.state.figure_id, name).axes(self.state.index)
    }

    fn send(&self, u: PropertyUpdate) -> Result<&Self> {
        self.session().update(u)?;
        Ok(self)
    }

    fn need_2d(&self, what: &str) -> Result<()> {
        if self.is_3d() {
            Err(Error::invalid(format!("{what} needs a 2D axes")))
        } else {
            Ok(())
        }
    }

    fn need_3d(&self, what: &str) -> Result<()> {
        if self.is_3d() {
            Ok(())
        } else {
            Err(Error::invalid(format!(
                "{what} needs a 3D axes (Figure::subplot3d)"
            )))
        }
    }

    fn add_series(&self, kind: SeriesKind, label: &str) -> Result<Series> {
        let payload =
            codec::encode_add_series(self.figure_id(), self.index(), kind.wire_name(), label);
        let resp = self.session().request(p::REQ_ADD_SERIES, &payload)?;
        let idx = codec::decode_series_added(&resp.payload)?;
        let state = Arc::new(SeriesState::new(idx, kind, label));
        lock(&self.state.series).push(state.clone());
        Ok(Series {
            axes: self.clone(),
            state,
        })
    }

    // ─── 2D series ──────────────────────────────────────────────────────

    /// Add a line through `(x[i], y[i])`.
    pub fn line(&self, x: &(impl Values + ?Sized), y: &(impl Values + ?Sized)) -> Result<Series> {
        self.xy_series(SeriesKind::Line, x, y)
    }

    /// Add markers at `(x[i], y[i])`.
    pub fn scatter(
        &self,
        x: &(impl Values + ?Sized),
        y: &(impl Values + ?Sized),
    ) -> Result<Series> {
        self.xy_series(SeriesKind::Scatter, x, y)
    }

    fn xy_series(
        &self,
        kind: SeriesKind,
        x: &(impl Values + ?Sized),
        y: &(impl Values + ?Sized),
    ) -> Result<Series> {
        self.need_2d(kind.wire_name())?;
        let (x, y) = (x.as_f64(), y.as_f64());
        if x.len() != y.len() {
            return Err(Error::invalid(format!(
                "x has {} values but y has {}",
                x.len(),
                y.len()
            )));
        }
        let s = self.add_series(kind, "")?;
        s.set_data_f64(&x, &y)?;
        self.fit_points(&x, &y)?;
        Ok(s)
    }

    /// Add a line styled by a MATLAB / matplotlib format string such as
    /// `"r--o"` (red, dashed, circles) or `"g^"` (green triangles, no line).
    /// `""` keeps the defaults.
    pub fn plot(
        &self,
        x: &(impl Values + ?Sized),
        y: &(impl Values + ?Sized),
        fmt: &str,
    ) -> Result<Series> {
        let f = Format::parse(fmt)?;
        let s = self.line(x, y)?;
        if f != Format::default() {
            s.apply_format(&f)?;
        }
        Ok(s)
    }

    /// Plot `y` against `0, 1, 2, ...`.
    pub fn plot_y(&self, y: &(impl Values + ?Sized), fmt: &str) -> Result<Series> {
        let x = data::arange(y.len());
        self.plot(&x, y, fmt)
    }

    /// Step plot: the value changes in steps (`DrawStyle::StepsPre` holds
    /// each y over the interval ending at its x, `StepsPost` until the next x).
    pub fn step(
        &self,
        x: &(impl Values + ?Sized),
        y: &(impl Values + ?Sized),
        style: DrawStyle,
    ) -> Result<Series> {
        let s = self.line(x, y)?;
        s.set_draw_style(style)?;
        Ok(s)
    }

    /// Stairs: `values[i]` holds from `edges[i]` to `edges[i + 1]`.
    pub fn stairs(
        &self,
        values: &(impl Values + ?Sized),
        edges: &(impl Values + ?Sized),
    ) -> Result<Series> {
        let v = values.as_f64();
        let e = edges.as_f64();
        if v.is_empty() || e.len() != v.len() + 1 {
            return Err(Error::invalid(
                "stairs needs values and len(edges) == len(values) + 1",
            ));
        }
        let mut y = v.to_vec();
        y.push(*v.last().unwrap());
        self.step(e.as_ref(), &y, DrawStyle::StepsPost)
    }

    /// Plot `y = f(x)` at `n` points over `[xmin, xmax]`.
    pub fn fplot(&self, f: impl Fn(f64) -> f64, xmin: f64, xmax: f64, n: usize) -> Result<Series> {
        let xmax = if xmin == xmax { xmin + 1.0 } else { xmax };
        let xs = data::linspace(xmin, xmax, n.max(2));
        let ys: Vec<f64> = xs.iter().map(|&x| f(x)).collect();
        self.line(&xs, &ys)
    }

    /// Horizontal reference line at `y` (does not affect automatic limits).
    pub fn hline(&self, y: f64) -> Result<Series> {
        self.need_2d("hline")?;
        let s = self.add_series(SeriesKind::Line, "")?;
        s.set_data_f64(&[-REF_SPAN, REF_SPAN], &[y, y])?;
        s.set_color("gray")?.set_line_width(1.0)?;
        Ok(s)
    }

    /// Vertical reference line at `x` (does not affect automatic limits).
    pub fn vline(&self, x: f64) -> Result<Series> {
        self.need_2d("vline")?;
        let s = self.add_series(SeriesKind::Line, "")?;
        s.set_data_f64(&[x, x], &[-REF_SPAN, REF_SPAN])?;
        s.set_color("gray")?.set_line_width(1.0)?;
        Ok(s)
    }

    fn stats_series(&self, kind: SeriesKind, label: &str, payload: &[f32]) -> Result<Series> {
        self.need_2d(kind.wire_name())?;
        let s = self.add_series(kind, label)?;
        s.set_payload(payload)?;
        Ok(s)
    }

    /// Vertical bars of `heights` centred at `x`.
    pub fn bar(
        &self,
        x: &(impl Values + ?Sized),
        heights: &(impl Values + ?Sized),
    ) -> Result<Series> {
        self.bar_with(x, heights, &BarOptions::default())
    }

    /// Horizontal bars of `widths` centred at `y`.
    pub fn barh(
        &self,
        y: &(impl Values + ?Sized),
        widths: &(impl Values + ?Sized),
    ) -> Result<Series> {
        self.bar_with(
            y,
            widths,
            &BarOptions {
                horizontal: true,
                ..Default::default()
            },
        )
    }

    /// Bars with options (width, baseline, orientation, fill style).
    ///
    /// Several bar series at the same positions are grouped side by side;
    /// see [`Axes::set_bar_mode`] to stack or overlay them.
    pub fn bar_with(
        &self,
        x: &(impl Values + ?Sized),
        heights: &(impl Values + ?Sized),
        opts: &BarOptions,
    ) -> Result<Series> {
        let (x, h) = (x.as_f64(), heights.as_f64());
        self.bar_impl(&x, &h, opts, None)
    }

    /// Bars at `0, 1, 2, ...` labelled with category names.
    pub fn bar_categories(
        &self,
        names: &[&str],
        heights: &(impl Values + ?Sized),
        opts: &BarOptions,
    ) -> Result<Series> {
        let x = data::arange(names.len());
        self.bar_impl(&x, &heights.as_f64(), opts, Some(names))
    }

    fn bar_impl(
        &self,
        x: &[f64],
        h: &[f64],
        opts: &BarOptions,
        names: Option<&[&str]>,
    ) -> Result<Series> {
        if x.len() != h.len() {
            return Err(Error::invalid(format!(
                "x has {} values but heights has {}",
                x.len(),
                h.len()
            )));
        }
        let mut payload = vec![
            opts.width as f32,
            opts.bottom as f32,
            if opts.horizontal { 1.0 } else { 0.0 },
            0.0,
            x.len() as f32,
        ];
        payload.extend(x.iter().map(|v| *v as f32));
        payload.extend(h.iter().map(|v| *v as f32));
        payload.extend(opts.fill.trailer()?);
        let s = self.stats_series(SeriesKind::Bar, &opts.label, &payload)?;
        if let Some(names) = names {
            s.set_categories(names)?;
        }
        lock(&self.state.fit).bars.push(BarRec {
            pos: x.to_vec(),
            heights: h.to_vec(),
            bottom: opts.bottom,
            horizontal: opts.horizontal,
            width: opts.width,
        });
        self.refit_bars()?;
        Ok(s)
    }

    /// How several bar series at the same positions are drawn.
    pub fn set_bar_mode(&self, mode: BarMode) -> Result<&Self> {
        self.send(self.prop("bar_mode").f(mode.wire() as f64))?;
        let changed = std::mem::replace(&mut *lock(&self.state.bar_mode), mode) != mode;
        if changed {
            self.refit_bars()?;
        }
        Ok(self)
    }

    /// The current bar mode.
    pub fn bar_mode(&self) -> BarMode {
        *lock(&self.state.bar_mode)
    }

    fn refit_bars(&self) -> Result<()> {
        let mode = self.bar_mode();
        let bars = lock(&self.state.fit).bars.clone();
        for horizontal in [false, true] {
            let same: Vec<&BarRec> = bars.iter().filter(|b| b.horizontal == horizontal).collect();
            if same.is_empty() {
                continue;
            }
            let (ext, origins) = bar_extent(&same, mode);
            let pos: Vec<f64> = same
                .iter()
                .flat_map(|b| {
                    b.pos
                        .iter()
                        .flat_map(move |p| [p - b.width / 2.0, p + b.width / 2.0])
                })
                .collect();
            let sticky = (
                origins.first().copied().unwrap_or(0.0),
                origins.last().copied().unwrap_or(0.0),
            );
            if horizontal {
                self.accumulate(&ext, &pos, Some(sticky), None);
            } else {
                self.accumulate(&pos, &ext, None, Some(sticky));
            }
        }
        self.apply_fit()
    }

    /// Histogram of `values` in `bins` equal-width bins over their range.
    pub fn hist(&self, values: &(impl Values + ?Sized), bins: u32) -> Result<Series> {
        self.hist_with(
            values,
            &HistOptions {
                bins,
                ..Default::default()
            },
        )
    }

    /// Histogram with options (density, cumulative, fill style).
    pub fn hist_with(&self, values: &(impl Values + ?Sized), opts: &HistOptions) -> Result<Series> {
        if opts.bins < 1 {
            return Err(Error::invalid("bins must be >= 1"));
        }
        let vals: Vec<f64> = values
            .as_f64()
            .iter()
            .copied()
            .filter(|v| v.is_finite())
            .collect();
        if vals.is_empty() {
            return Err(Error::invalid("hist needs at least one finite value"));
        }
        let mut payload = vec![
            opts.bins as f32,
            if opts.cumulative { 1.0 } else { 0.0 },
            if opts.density { 1.0 } else { 0.0 },
            0.0,
            vals.len() as f32,
        ];
        payload.extend(vals.iter().map(|v| *v as f32));
        payload.extend(opts.fill.trailer()?);
        let s = self.stats_series(SeriesKind::Histogram, &opts.label, &payload)?;
        let (lo, hi, counts) =
            histogram_counts(&vals, opts.bins as usize, opts.density, opts.cumulative);
        let top = counts.iter().copied().fold(0.0, f64::max);
        self.accumulate(&[lo, hi], &[0.0, top], None, Some((0.0, 0.0)));
        self.apply_fit()?;
        Ok(s)
    }

    /// Fill between `lower` and `upper` over `x` (matplotlib `fill_between`).
    pub fn fill_between(
        &self,
        x: &(impl Values + ?Sized),
        lower: &(impl Values + ?Sized),
        upper: &(impl Values + ?Sized),
    ) -> Result<Series> {
        self.fill_between_with(x, lower, upper, &BandOptions::default())
    }

    /// [`Axes::fill_between`] with options.
    pub fn fill_between_with(
        &self,
        x: &(impl Values + ?Sized),
        y1: &(impl Values + ?Sized),
        y2: &(impl Values + ?Sized),
        opts: &BandOptions,
    ) -> Result<Series> {
        let (x, a, b) = (x.as_f64(), y1.as_f64(), y2.as_f64());
        let b: Vec<f64> = if b.len() == 1 && x.len() != 1 {
            vec![b[0]; x.len()]
        } else {
            b.to_vec()
        };
        if !(x.len() == a.len() && a.len() == b.len()) {
            return Err(Error::invalid(format!(
                "x, y1 and y2 must have the same length ({}, {}, {})",
                x.len(),
                a.len(),
                b.len()
            )));
        }
        let lo: Vec<f64> = a.iter().zip(&b).map(|(p, q)| p.min(*q)).collect();
        let hi: Vec<f64> = a.iter().zip(&b).map(|(p, q)| p.max(*q)).collect();
        let mut payload = vec![
            opts.alpha,
            opts.edge_width,
            if opts.edges { 1.0 } else { 0.0 },
            0.0,
            x.len() as f32,
        ];
        for col in [&x[..], &lo[..], &hi[..]] {
            payload.extend(col.iter().map(|v| *v as f32));
        }
        let s = self.stats_series(SeriesKind::Band, &opts.label, &payload)?;
        // A constant edge (fill down to a baseline) is sticky, like a bar baseline.
        let sticky = [&lo, &hi]
            .iter()
            .filter(|e| !e.is_empty() && e.iter().all(|v| *v == e[0]))
            .map(|e| e[0])
            .fold(None, |acc: Option<(f64, f64)>, v| Some(merge(acc, v, v)));
        let xx: Vec<f64> = x.iter().chain(x.iter()).copied().collect();
        let yy: Vec<f64> = lo.iter().chain(hi.iter()).copied().collect();
        self.accumulate(&xx, &yy, None, sticky);
        self.apply_fit()?;
        Ok(s)
    }

    /// Stems from 0 to `y` with round heads.
    pub fn stem(&self, x: &(impl Values + ?Sized), y: &(impl Values + ?Sized)) -> Result<Series> {
        self.stem_with(x, y, &StemOptions::default())
    }

    /// [`Axes::stem`] with options.
    pub fn stem_with(
        &self,
        x: &(impl Values + ?Sized),
        y: &(impl Values + ?Sized),
        opts: &StemOptions,
    ) -> Result<Series> {
        let (x, y) = (x.as_f64(), y.as_f64());
        if x.len() != y.len() {
            return Err(Error::invalid(format!(
                "x has {} values but y has {}",
                x.len(),
                y.len()
            )));
        }
        let mut payload = vec![
            opts.baseline as f32,
            opts.stem_width.max(0.0),
            if opts.heads { 1.0 } else { 0.0 },
            0.0,
            x.len() as f32,
        ];
        payload.extend(x.iter().map(|v| *v as f32));
        payload.extend(y.iter().map(|v| *v as f32));
        let s = self.stats_series(SeriesKind::Stem, &opts.label, &payload)?;
        let mut yy = y.to_vec();
        yy.push(opts.baseline);
        self.accumulate(&x, &yy, None, Some((opts.baseline, opts.baseline)));
        self.apply_fit()?;
        Ok(s)
    }

    /// Box-and-whisker plot of each data set (quartiles, 1.5 IQR whiskers, outliers).
    pub fn boxplot<V: Values>(&self, datasets: &[V]) -> Result<Series> {
        self.boxplot_with(datasets, &BoxOptions::default())
    }

    /// [`Axes::boxplot`] with options.
    pub fn boxplot_with<V: Values>(&self, datasets: &[V], opts: &BoxOptions) -> Result<Series> {
        let positions = positions_for(datasets.len(), opts.positions.as_deref())?;
        let mut payload = vec![
            opts.width,
            if opts.outliers { 1.0 } else { 0.0 },
            if opts.notched { 1.0 } else { 0.0 },
            0.0,
            0.0,
        ];
        let (mut n, mut ys, mut xs) = (0usize, Vec::new(), Vec::new());
        for (pos, d) in positions.iter().zip(datasets) {
            let st = box_stats(&d.as_f64());
            let Some(st) = st else { continue };
            n += 1;
            payload.extend_from_slice(&[
                *pos as f32,
                st.median as f32,
                st.q1 as f32,
                st.q3 as f32,
                st.whisker_low as f32,
                st.whisker_high as f32,
                st.count as f32,
                st.outliers.len() as f32,
            ]);
            payload.extend(st.outliers.iter().map(|v| *v as f32));
            ys.extend([st.whisker_low, st.whisker_high]);
            if opts.outliers {
                ys.extend(st.outliers.iter().copied());
            }
            xs.extend([pos - opts.width as f64 / 2.0, pos + opts.width as f64 / 2.0]);
        }
        payload[4] = n as f32;
        payload.extend(opts.fill.trailer()?);
        let s = self.stats_series(SeriesKind::Box, &opts.label, &payload)?;
        self.accumulate(&xs, &ys, None, None);
        self.apply_fit()?;
        Ok(s)
    }

    /// Violin plot (mirrored density estimate) of each data set.
    pub fn violin<V: Values>(&self, datasets: &[V]) -> Result<Series> {
        self.violin_with(datasets, &ViolinOptions::default())
    }

    /// [`Axes::violin`] with options.
    pub fn violin_with<V: Values>(&self, datasets: &[V], opts: &ViolinOptions) -> Result<Series> {
        let positions = positions_for(datasets.len(), opts.positions.as_deref())?;
        let mut payload = vec![
            opts.width,
            opts.resolution.clamp(2, 10_000) as f32,
            if opts.show_box { 1.0 } else { 0.0 },
            0.0,
            0.0,
        ];
        let (mut n, mut xs, mut ys) = (0usize, Vec::new(), Vec::new());
        for (pos, d) in positions.iter().zip(datasets) {
            let vals: Vec<f64> = d
                .as_f64()
                .iter()
                .copied()
                .filter(|v| v.is_finite())
                .collect();
            if vals.is_empty() {
                continue;
            }
            n += 1;
            payload.push(*pos as f32);
            payload.push(vals.len() as f32);
            payload.extend(vals.iter().map(|v| *v as f32));
            ys.extend_from_slice(&vals);
            xs.extend([pos - opts.width as f64 / 2.0, pos + opts.width as f64 / 2.0]);
        }
        payload[4] = n as f32;
        payload.extend(opts.fill.trailer()?);
        let s = self.stats_series(SeriesKind::Violin, &opts.label, &payload)?;
        self.accumulate(&xs, &ys, None, None);
        self.apply_fit()?;
        Ok(s)
    }

    /// Line through the points with symmetric vertical error bars.
    pub fn errorbar(
        &self,
        x: &(impl Values + ?Sized),
        y: &(impl Values + ?Sized),
        yerr: &(impl Values + ?Sized),
    ) -> Result<ErrorBar> {
        let e = yerr.as_f64();
        let spec = if e.len() == 1 {
            ErrorSpec::Scalar(e[0])
        } else {
            ErrorSpec::Symmetric(e.to_vec())
        };
        self.errorbar_with(
            x,
            y,
            &ErrorBarOptions {
                yerr: Some(spec),
                ..Default::default()
            },
        )
    }

    /// Error bars with options (x and/or y errors, caps, point style, colour).
    pub fn errorbar_with(
        &self,
        x: &(impl Values + ?Sized),
        y: &(impl Values + ?Sized),
        opts: &ErrorBarOptions,
    ) -> Result<ErrorBar> {
        self.need_2d("errorbar")?;
        let (xv, yv) = (x.as_f64(), y.as_f64());
        if xv.len() != yv.len() {
            return Err(Error::invalid(format!(
                "x has {} values but y has {}",
                xv.len(),
                yv.len()
            )));
        }
        let n = xv.len();
        let nan = f64::NAN;
        let half_cap = opts.capsize.abs() / 2.0;
        let (mut bx, mut by) = (Vec::new(), Vec::new());
        if let Some(spec) = &opts.yerr {
            let (lo, hi) = spec.expand(n, "yerr")?;
            for i in 0..n {
                let (xi, yi) = (xv[i], yv[i]);
                bx.extend([xi, xi, nan]);
                by.extend([yi - lo[i], yi + hi[i], nan]);
                if half_cap > 0.0 {
                    for yc in [yi - lo[i], yi + hi[i]] {
                        bx.extend([xi - half_cap, xi + half_cap, nan]);
                        by.extend([yc, yc, nan]);
                    }
                }
            }
        }
        if let Some(spec) = &opts.xerr {
            let (lo, hi) = spec.expand(n, "xerr")?;
            for i in 0..n {
                let (xi, yi) = (xv[i], yv[i]);
                bx.extend([xi - lo[i], xi + hi[i], nan]);
                by.extend([yi, yi, nan]);
                if half_cap > 0.0 {
                    for xc in [xi - lo[i], xi + hi[i]] {
                        bx.extend([xc, xc, nan]);
                        by.extend([yi - half_cap, yi + half_cap, nan]);
                    }
                }
            }
        }
        let color = match &opts.color {
            Some(c) => c.clone(),
            None => {
                let k = lock(&self.state.series).len();
                ERRORBAR_CYCLE[k % ERRORBAR_CYCLE.len()].to_string()
            }
        };
        let data = match opts.style {
            ErrorBarStyle::Line => Some(self.line(xv.as_ref(), yv.as_ref())?),
            ErrorBarStyle::Markers => {
                let s = self.scatter(xv.as_ref(), yv.as_ref())?;
                s.set_marker_size(6.0)?;
                Some(s)
            }
            ErrorBarStyle::None => None,
        };
        if let Some(s) = &data {
            s.set_color(color.as_str())?;
            if !opts.label.is_empty() {
                s.set_label(&opts.label)?;
            }
        }
        let bars = if bx.is_empty() {
            None
        } else {
            let s = self.line(&bx, &by)?;
            s.set_color(color.as_str())?;
            if data.is_none() && !opts.label.is_empty() {
                s.set_label(&opts.label)?;
            }
            Some(s)
        };
        Ok(ErrorBar { data, bars })
    }

    // ─── 3D series ──────────────────────────────────────────────────────

    /// 3D line through `(x[i], y[i], z[i])`.
    pub fn line3d(
        &self,
        x: &(impl Values + ?Sized),
        y: &(impl Values + ?Sized),
        z: &(impl Values + ?Sized),
    ) -> Result<Series> {
        self.xyz_series(SeriesKind::Line3d, x, y, z)
    }

    /// 3D markers at `(x[i], y[i], z[i])`.
    pub fn scatter3d(
        &self,
        x: &(impl Values + ?Sized),
        y: &(impl Values + ?Sized),
        z: &(impl Values + ?Sized),
    ) -> Result<Series> {
        self.xyz_series(SeriesKind::Scatter3d, x, y, z)
    }

    fn xyz_series(
        &self,
        kind: SeriesKind,
        x: &(impl Values + ?Sized),
        y: &(impl Values + ?Sized),
        z: &(impl Values + ?Sized),
    ) -> Result<Series> {
        self.need_3d(kind.wire_name())?;
        if x.len() != y.len() || x.len() != z.len() {
            return Err(Error::invalid(format!(
                "x, y and z must have the same length ({}, {}, {})",
                x.len(),
                y.len(),
                z.len()
            )));
        }
        let s = self.add_series(kind, "")?;
        s.set_data_xyz(x, y, z)?;
        Ok(s)
    }

    /// Surface over a grid: `x` has `nx` values, `y` has `ny`, and `z` holds
    /// `ny * nx` heights row by row (`z[r * nx + c]` is the height at
    /// `(x[c], y[r])`). With the `ndarray` feature `z` can be an `(ny, nx)` array.
    pub fn surface(
        &self,
        x: &(impl Values + ?Sized),
        y: &(impl Values + ?Sized),
        z: &(impl Values + ?Sized),
    ) -> Result<Series> {
        let (xg, yg, zv) = (x.as_f64(), y.as_f64(), z.as_f64());
        let (nx, ny) = (xg.len(), yg.len());
        if nx < 2 || ny < 2 || zv.len() != nx * ny {
            return Err(Error::invalid(format!(
                "surface needs x (nx >= 2), y (ny >= 2) and nx * ny = {} heights, got {}",
                nx * ny,
                zv.len()
            )));
        }
        let mut xx = Vec::with_capacity(nx * ny);
        let mut yy = Vec::with_capacity(nx * ny);
        for r in 0..ny {
            for c in 0..nx {
                xx.push(xg[c]);
                yy.push(yg[r]);
            }
        }
        self.surface_mesh(&xx, &yy, zv.as_ref())
    }

    /// Surface from raveled meshgrid arrays (equal length; like
    /// `np.meshgrid(...)` flattened).
    pub fn surface_mesh(
        &self,
        x: &(impl Values + ?Sized),
        y: &(impl Values + ?Sized),
        z: &(impl Values + ?Sized),
    ) -> Result<Series> {
        self.need_3d("surface")?;
        let s = self.add_series(SeriesKind::Surface, "surface")?;
        s.set_data_xyz(x, y, z)?;
        Ok(s)
    }

    // ─── Topics ─────────────────────────────────────────────────────────

    /// Plot a live topic (see [`crate::Publisher`]): the broker feeds a new
    /// series in this axes with every sample published on `name`.
    pub fn subscribe_topic(&self, name: &str) -> Result<Series> {
        let payload = codec::encode_subscribe_topic(
            name,
            self.figure_id(),
            self.index(),
            p::SUBSCRIBE_NEW_SERIES,
        );
        let resp = self.session().request(p::REQ_SUBSCRIBE_TOPIC, &payload)?;
        let idx = codec::decode_subscribe_topic(&resp.payload)?;
        let kind = if self.is_3d() {
            SeriesKind::Line3d
        } else {
            SeriesKind::Line
        };
        let state = Arc::new(SeriesState::new(idx, kind, name));
        *lock(&state.topic) = Some(name.to_string());
        state.has_data.store(true, Ordering::SeqCst);
        lock(&self.state.series).push(state.clone());
        Ok(Series {
            axes: self.clone(),
            state,
        })
    }

    // ─── Limits, labels, scales ─────────────────────────────────────────

    fn set_lim(&self, axis: usize, name: &str, lo: f64, hi: f64) -> Result<&Self> {
        if !(lo.is_finite() && hi.is_finite()) || lo == hi {
            return Err(Error::invalid(format!(
                "{name} needs two different finite values"
            )));
        }
        self.send(self.prop(name).f2(lo, hi))?;
        let mut fit = lock(&self.state.fit);
        fit.auto[axis] = false;
        fit.sent[axis] = Some((lo, hi));
        Ok(self)
    }

    /// A second y axis on the right of this axes, sharing its x axis
    /// (matplotlib's `twinx`).
    ///
    /// Series added to the returned axes use its own y limits, ticks, scale
    /// and label (drawn on the right), while x limits, pan and zoom follow
    /// this axes.  Its series join this axes' legend and continue its colour
    /// cycle.  Calling it again returns the same twin; on a twin it returns
    /// that twin.
    pub fn twinx(&self) -> Result<Axes> {
        self.need_2d("twinx")?;
        if self.state.twin_of >= 0 {
            return Ok(self.clone());
        }
        if let Some(t) = lock(&self.state.twin).clone() {
            return Ok(Axes::from_state(self.figure.clone(), t));
        }
        let twin = self
            .figure
            .request_axes(1, 1, 1, false, self.index() as i32)?;
        *lock(&self.state.twin) = Some(twin.state.clone());
        // The twin's x axis is fitted with its host's (and the host's share group).
        let existing = lock(&self.state.share)[0].clone();
        let g = match existing {
            Some(g) => g,
            None => {
                let g = Arc::new(ShareGroup {
                    members: Mutex::new(vec![Arc::downgrade(&self.state)]),
                });
                lock(&self.state.share)[0] = Some(g.clone());
                g
            }
        };
        lock(&g.members).push(Arc::downgrade(&twin.state));
        lock(&twin.state.share)[0] = Some(g);
        self.apply_fit()?;
        Ok(twin)
    }

    /// The host of a twin made by [`Axes::twinx`], if this axes is one.
    fn twin_host(&self) -> Option<Axes> {
        if self.state.twin_of < 0 {
            return None;
        }
        lock(&self.figure.state.axes)
            .iter()
            .find(|a| a.index as i32 == self.state.twin_of)
            .map(|a| Axes::from_state(self.figure.clone(), a.clone()))
    }

    /// Fix the x limits (turns automatic x limits off; see [`Axes::autoscale`]).
    /// On a twin this sets its host's x limits.
    pub fn set_xlim(&self, lo: f64, hi: f64) -> Result<&Self> {
        if let Some(h) = self.twin_host() {
            h.set_xlim(lo, hi)?;
            return Ok(self);
        }
        self.set_lim(0, "xlim", lo, hi)
    }

    /// Fix the y limits.
    pub fn set_ylim(&self, lo: f64, hi: f64) -> Result<&Self> {
        self.set_lim(1, "ylim", lo, hi)
    }

    /// Fix the z limits (3D).
    pub fn set_zlim(&self, lo: f64, hi: f64) -> Result<&Self> {
        self.need_3d("set_zlim")?;
        self.set_lim(2, "zlim", lo, hi)
    }

    /// Turn automatic limits on (fit all data plotted so far, 5% padding) or off.
    pub fn autoscale(&self, enabled: bool) -> Result<&Self> {
        {
            let mut fit = lock(&self.state.fit);
            fit.auto = [enabled; 3];
            fit.sent = [None; 3];
        }
        if enabled {
            self.apply_fit()?;
        }
        Ok(self)
    }

    /// X axis label (on a twin: its host's).
    pub fn set_xlabel(&self, label: &str) -> Result<&Self> {
        if let Some(h) = self.twin_host() {
            h.set_xlabel(label)?;
            return Ok(self);
        }
        self.send(self.prop("xlabel").s(label))
    }

    /// Y axis label.
    pub fn set_ylabel(&self, label: &str) -> Result<&Self> {
        self.send(self.prop("ylabel").s(label))
    }

    /// Title above the axes (on a twin: its host's).
    pub fn set_title(&self, title: &str) -> Result<&Self> {
        if let Some(h) = self.twin_host() {
            h.set_title(title)?;
            return Ok(self);
        }
        self.send(self.prop("axes_title").s(title))
    }

    /// X axis scale.
    pub fn set_xscale(&self, scale: Scale) -> Result<&Self> {
        self.send(self.prop("xscale").f(scale.wire() as f64))?;
        lock(&self.state.fit).scales[0] = scale;
        self.apply_fit()?;
        Ok(self)
    }

    /// Y axis scale.
    pub fn set_yscale(&self, scale: Scale) -> Result<&Self> {
        self.send(self.prop("yscale").f(scale.wire() as f64))?;
        lock(&self.state.fit).scales[1] = scale;
        self.apply_fit()?;
        Ok(self)
    }

    /// Show x tick labels as dates/times (x values are Unix epoch seconds, UTC).
    pub fn set_xaxis_datetime(&self, enabled: bool) -> Result<&Self> {
        self.send(self.prop("xaxis_datetime").b(enabled))
    }

    /// Show y tick labels as dates/times.
    pub fn set_yaxis_datetime(&self, enabled: bool) -> Result<&Self> {
        self.send(self.prop("yaxis_datetime").b(enabled))
    }

    /// Show or hide the grid.
    pub fn grid(&self, visible: bool) -> Result<&Self> {
        self.send(self.prop("grid").b(visible))
    }

    /// Fixed x ticks at `ticks` (inside the limits), labelled with `labels`
    /// (same length) or the formatted values; `Some(&[])` hides the ticks and
    /// `None` goes back to automatic ticks.
    pub fn set_xticks(&self, ticks: Option<&[f64]>, labels: Option<&[&str]>) -> Result<&Self> {
        self.send_ticks(0, ticks, labels)
    }

    /// Fixed y ticks; see [`Axes::set_xticks`].
    pub fn set_yticks(&self, ticks: Option<&[f64]>, labels: Option<&[&str]>) -> Result<&Self> {
        self.send_ticks(1, ticks, labels)
    }

    fn send_ticks(
        &self,
        axis: u8,
        ticks: Option<&[f64]>,
        labels: Option<&[&str]>,
    ) -> Result<&Self> {
        self.need_2d("set_xticks / set_yticks")?;
        let pos = ticks.unwrap_or(&[]);
        if pos.iter().any(|p| !p.is_finite()) {
            return Err(Error::invalid("tick positions must be finite"));
        }
        let mut names = String::new();
        if let (Some(_), Some(labels)) = (ticks, labels) {
            if labels.len() != pos.len() {
                return Err(Error::invalid("tick labels must match the tick positions"));
            }
            let cleaned: Vec<String> = labels
                .iter()
                .map(|l| {
                    let l = l.replace('\u{1f}', " ");
                    if l.is_empty() {
                        " ".to_string()
                    } else {
                        l
                    }
                })
                .collect();
            names = cleaned.join("\u{1f}");
        }
        let origin = pos.first().copied().unwrap_or(0.0);
        let data = pos.iter().map(|p| (p - origin) as f32).collect();
        self.send(
            self.prop("ticks")
                .f2(axis as f64, origin)
                .b(ticks.is_some())
                .data(data)
                .s(names),
        )
    }

    /// Show or hide a colorbar for the axes' colormapped series.
    pub fn colorbar(&self, visible: bool) -> Result<&Self> {
        self.send(self.prop("colorbar").b(visible))
    }

    /// Show or hide the legend.
    pub fn legend(&self, visible: bool) -> Result<&Self> {
        self.send(self.prop("legend").b(visible))
    }

    /// 3D light placement.
    pub fn set_light_mode(&self, mode: LightMode) -> Result<&Self> {
        self.need_3d("set_light_mode")?;
        self.send(self.prop("light_mode").s(mode.token()))
    }

    /// 3D fixed light direction: azimuth (degrees from +X towards +Y) and
    /// elevation (degrees above the XY plane).
    pub fn set_light_angles(&self, azimuth: f64, elevation: f64) -> Result<&Self> {
        self.need_3d("set_light_angles")?;
        self.send(self.prop("light_angles").f2(azimuth, elevation))
    }

    /// 3D fog: fade series towards the background with distance, 0 (off) to 1.
    pub fn set_fog(&self, strength: f64) -> Result<&Self> {
        self.need_3d("set_fog")?;
        if !(0.0..=1.0).contains(&strength) {
            return Err(Error::invalid("fog strength must be in [0, 1]"));
        }
        self.send(self.prop("fog").f(strength))
    }

    /// Send several axes settings in one message.
    ///
    /// ```no_run
    /// # use spectra_plot::*;
    /// # fn f(ax: &Axes) -> Result<()> {
    /// ax.batch(|b| {
    ///     b.xlabel("time (s)").ylabel("volts").title("Scope").grid(true).xlim(0.0, 10.0);
    /// })?;
    /// # Ok(()) }
    /// ```
    pub fn batch(&self, build: impl FnOnce(&mut AxesBatch)) -> Result<&Self> {
        let mut b = AxesBatch {
            axes: self.clone(),
            updates: Vec::new(),
            lims: Vec::new(),
        };
        build(&mut b);
        self.session().update_batch(&b.updates)?;
        let mut fit = lock(&self.state.fit);
        for (axis, lo, hi) in b.lims {
            fit.auto[axis] = false;
            fit.sent[axis] = Some((lo, hi));
        }
        Ok(self)
    }

    // ─── Annotations ────────────────────────────────────────────────────

    /// Text with its anchor at data coordinates `(x, y)` (2D axes).
    pub fn text(&self, x: f64, y: f64, text: &str) -> Result<TextAnnotation> {
        self.text_with(x, y, text, &TextOptions::default())
    }

    /// [`Axes::text`] with colour, size, alignment, box, halo and rotation.
    pub fn text_with(
        &self,
        x: f64,
        y: f64,
        text: &str,
        opts: &TextOptions,
    ) -> Result<TextAnnotation> {
        self.add_annotation(x, y, text, opts.clone(), None)
    }

    /// Text centred at `text_at` with an arrow pointing to `target`.
    pub fn annotate(
        &self,
        text: &str,
        target: (f64, f64),
        text_at: (f64, f64),
    ) -> Result<TextAnnotation> {
        self.annotate_with(text, target, text_at, &TextOptions::default())
    }

    /// [`Axes::annotate`] with options (alignment is centred).
    pub fn annotate_with(
        &self,
        text: &str,
        target: (f64, f64),
        text_at: (f64, f64),
        opts: &TextOptions,
    ) -> Result<TextAnnotation> {
        let mut o = opts.clone();
        o.halign = crate::style::HAlign::Center;
        o.valign = crate::style::VAlign::Middle;
        self.add_annotation(text_at.0, text_at.1, text, o, Some(target))
    }

    fn add_annotation(
        &self,
        x: f64,
        y: f64,
        text: &str,
        opts: TextOptions,
        arrow: Option<(f64, f64)>,
    ) -> Result<TextAnnotation> {
        self.need_2d("text annotations")?;
        let mut list = lock(&self.state.annotations);
        let state = Arc::new(Mutex::new(AnnotationState {
            index: list.len() as i64,
            x,
            y,
            text: text.to_string(),
            opts,
            arrow,
            visible: true,
        }));
        let ann = TextAnnotation {
            axes: self.clone(),
            state: state.clone(),
        };
        ann.send(true)?;
        list.push(state);
        Ok(ann)
    }

    /// Text annotations of this axes.
    pub fn annotations(&self) -> Vec<TextAnnotation> {
        lock(&self.state.annotations)
            .iter()
            .map(|s| TextAnnotation {
                axes: self.clone(),
                state: s.clone(),
            })
            .collect()
    }

    /// Remove every text annotation.
    pub fn clear_annotations(&self) -> Result<&Self> {
        self.send(self.prop("clear_annotations"))?;
        for a in lock(&self.state.annotations).drain(..) {
            lock(&a).index = -1;
        }
        Ok(self)
    }

    // ─── Removal ────────────────────────────────────────────────────────

    /// Remove every series from this axes (automatic limits start over).
    pub fn clear(&self) -> Result<&Self> {
        let mut idx: Vec<u32> = lock(&self.state.series)
            .iter()
            .map(|s| s.index.load(Ordering::SeqCst))
            .collect();
        idx.sort_unstable_by(|a, b| b.cmp(a));
        for i in idx {
            self.session().request(
                p::REQ_REMOVE_SERIES,
                &codec::encode_remove_series(self.figure_id(), i),
            )?;
            self.on_series_removed(i);
        }
        let mut fit = lock(&self.state.fit);
        fit.bounds = [None; 3];
        fit.pos = [None; 2];
        fit.sticky = [None; 2];
        fit.bars.clear();
        Ok(self)
    }

    /// Mirror the backend after removing figure-wide series `index`: later
    /// series in every axes of the figure shift down by one.
    pub(crate) fn on_series_removed(&self, index: u32) {
        for ax in lock(&self.figure.state.axes).iter() {
            let mut list = lock(&ax.series);
            list.retain(|s| {
                if s.index.load(Ordering::SeqCst) == index {
                    s.removed.store(true, Ordering::SeqCst);
                    false
                } else {
                    true
                }
            });
            for s in list.iter() {
                let i = s.index.load(Ordering::SeqCst);
                if i > index {
                    s.index.store(i - 1, Ordering::SeqCst);
                }
            }
        }
    }

    // ─── Automatic limits ───────────────────────────────────────────────

    fn accumulate(
        &self,
        x: &[f64],
        y: &[f64],
        sticky_x: Option<(f64, f64)>,
        sticky_y: Option<(f64, f64)>,
    ) {
        let mut fit = lock(&self.state.fit);
        for (axis, st) in [(0usize, sticky_x), (1usize, sticky_y)] {
            if let Some((lo, hi)) = st {
                fit.sticky[axis] = Some(merge(fit.sticky[axis], lo, hi));
            }
        }
        let (Some(bx), Some(by)) = (data::finite_bounds(x), data::finite_bounds(y)) else {
            return;
        };
        fit.bounds[0] = Some(merge(fit.bounds[0], bx.0, bx.1));
        fit.bounds[1] = Some(merge(fit.bounds[1], by.0, by.1));
        fit.pos[0] = min_opt(fit.pos[0], bx.2);
        fit.pos[1] = min_opt(fit.pos[1], by.2);
    }

    pub(crate) fn fit_points(&self, x: &[f64], y: &[f64]) -> Result<()> {
        if self.is_3d() {
            return Ok(());
        }
        self.accumulate(x, y, None, None);
        self.apply_fit()
    }

    pub(crate) fn fit_points3(&self, x: &[f64], y: &[f64], z: &[f64]) -> Result<()> {
        let (Some(bx), Some(by), Some(bz)) = (
            data::finite_bounds(x),
            data::finite_bounds(y),
            data::finite_bounds(z),
        ) else {
            return Ok(());
        };
        let ups = {
            let mut fit = lock(&self.state.fit);
            let mut ups = Vec::new();
            for (axis, b, name) in [(0usize, bx, "xlim"), (1, by, "ylim"), (2, bz, "zlim")] {
                let (lo, hi) = merge(fit.bounds[axis], b.0, b.1);
                fit.bounds[axis] = Some((lo, hi));
                let pad = if hi != lo { (hi - lo) * 0.05 } else { 0.5 };
                let lim = (lo - pad, hi + pad);
                if fit.auto[axis] && fit.sent[axis] != Some(lim) {
                    fit.sent[axis] = Some(lim);
                    ups.push(self.prop(name).f2(lim.0, lim.1));
                }
            }
            ups
        };
        self.session().update_batch(&ups)
    }

    /// Send automatic x/y limits for this axes and its share groups.
    pub(crate) fn apply_fit(&self) -> Result<()> {
        if self.is_3d() {
            return Ok(());
        }
        let mut ups = Vec::new();
        for (axis, name) in [(0usize, "xlim"), (1usize, "ylim")] {
            let targets = self.state.group(axis);
            let (mut b, mut pos, mut sticky) = (None, None, None);
            let mut scale = Scale::Linear;
            for t in &targets {
                let f = lock(&t.fit);
                if let Some((lo, hi)) = f.bounds[axis] {
                    b = Some(merge(b, lo, hi));
                }
                pos = min_opt(pos, f.pos[axis]);
                if let Some((lo, hi)) = f.sticky[axis] {
                    sticky = Some(merge(sticky, lo, hi));
                }
                if Arc::ptr_eq(t, &self.state) {
                    scale = f.scales[axis];
                }
            }
            let Some((lo, hi)) = b else { continue };
            let Some(rng) = fit_range(lo, hi, pos, scale) else {
                continue;
            };
            let rng = stick(rng, lo, hi, sticky, scale);
            for t in &targets {
                let mut f = lock(&t.fit);
                if f.auto[axis] && f.sent[axis] != Some(rng) {
                    f.sent[axis] = Some(rng);
                    ups.push(
                        PropertyUpdate::new(t.figure_id, name)
                            .axes(t.index)
                            .f2(rng.0, rng.1),
                    );
                }
            }
        }
        self.session().update_batch(&ups)
    }
}

/// Collects axes settings for [`Axes::batch`].
pub struct AxesBatch {
    axes: Axes,
    updates: Vec<PropertyUpdate>,
    lims: Vec<(usize, f64, f64)>,
}

impl AxesBatch {
    fn push(&mut self, u: PropertyUpdate) -> &mut Self {
        self.updates.push(u);
        self
    }
    /// See [`Axes::set_xlim`].
    pub fn xlim(&mut self, lo: f64, hi: f64) -> &mut Self {
        self.lims.push((0, lo, hi));
        let u = self.axes.prop("xlim").f2(lo, hi);
        self.push(u)
    }
    /// See [`Axes::set_ylim`].
    pub fn ylim(&mut self, lo: f64, hi: f64) -> &mut Self {
        self.lims.push((1, lo, hi));
        let u = self.axes.prop("ylim").f2(lo, hi);
        self.push(u)
    }
    /// See [`Axes::set_zlim`].
    pub fn zlim(&mut self, lo: f64, hi: f64) -> &mut Self {
        self.lims.push((2, lo, hi));
        let u = self.axes.prop("zlim").f2(lo, hi);
        self.push(u)
    }
    /// See [`Axes::set_xlabel`].
    pub fn xlabel(&mut self, s: &str) -> &mut Self {
        let u = self.axes.prop("xlabel").s(s);
        self.push(u)
    }
    /// See [`Axes::set_ylabel`].
    pub fn ylabel(&mut self, s: &str) -> &mut Self {
        let u = self.axes.prop("ylabel").s(s);
        self.push(u)
    }
    /// See [`Axes::set_title`].
    pub fn title(&mut self, s: &str) -> &mut Self {
        let u = self.axes.prop("axes_title").s(s);
        self.push(u)
    }
    /// See [`Axes::grid`].
    pub fn grid(&mut self, visible: bool) -> &mut Self {
        let u = self.axes.prop("grid").b(visible);
        self.push(u)
    }
    /// See [`Axes::legend`].
    pub fn legend(&mut self, visible: bool) -> &mut Self {
        let u = self.axes.prop("legend").b(visible);
        self.push(u)
    }
}

// ─── Statistics helpers ──────────────────────────────────────────────────────

fn positions_for(n: usize, given: Option<&[f64]>) -> Result<Vec<f64>> {
    match given {
        Some(p) if p.len() != n => Err(Error::invalid(format!(
            "{} positions given for {n} data sets",
            p.len()
        ))),
        Some(p) => Ok(p.to_vec()),
        None => Ok((1..=n).map(|i| i as f64).collect()),
    }
}

struct BoxStats {
    median: f64,
    q1: f64,
    q3: f64,
    whisker_low: f64,
    whisker_high: f64,
    count: usize,
    outliers: Vec<f64>,
}

fn percentile(sorted: &[f64], p: f64) -> f64 {
    let idx = p * (sorted.len() - 1) as f64;
    let (lo, hi) = (idx.floor() as usize, idx.ceil() as usize);
    if lo == hi {
        sorted[lo]
    } else {
        let f = idx - lo as f64;
        sorted[lo] * (1.0 - f) + sorted[hi] * f
    }
}

/// Port of `BoxPlotSeries::compute_stats` (src/core/series_stats.cpp).
fn box_stats(values: &[f64]) -> Option<BoxStats> {
    let mut v: Vec<f64> = values.iter().copied().filter(|x| x.is_finite()).collect();
    if v.is_empty() {
        return None;
    }
    v.sort_by(f64::total_cmp);
    let (q1, median, q3) = (
        percentile(&v, 0.25),
        percentile(&v, 0.5),
        percentile(&v, 0.75),
    );
    let iqr = q3 - q1;
    let (lf, hf) = (q1 - 1.5 * iqr, q3 + 1.5 * iqr);
    let whisker_low = v.iter().copied().find(|x| *x >= lf).unwrap_or(v[0]);
    let whisker_high = v
        .iter()
        .rev()
        .copied()
        .find(|x| *x <= hf)
        .unwrap_or(v[v.len() - 1]);
    let outliers = v.iter().copied().filter(|x| *x < lf || *x > hf).collect();
    Some(BoxStats {
        median,
        q1,
        q3,
        whisker_low,
        whisker_high,
        count: v.len(),
        outliers,
    })
}

/// `(lo, hi, per-bin heights)` as the native histogram computes them.
fn histogram_counts(
    values: &[f64],
    bins: usize,
    density: bool,
    cumulative: bool,
) -> (f64, f64, Vec<f64>) {
    let lo = values.iter().copied().fold(f64::INFINITY, f64::min);
    let mut hi = values.iter().copied().fold(f64::NEG_INFINITY, f64::max);
    if lo == hi {
        hi = lo + 1f64.max(lo.abs() * 1e-6);
    }
    let w = (hi - lo) / bins as f64;
    let mut counts = vec![0.0; bins];
    for &v in values {
        let t = (v - lo) / w;
        let i = if t.is_nan() || t >= bins as f64 {
            bins - 1
        } else if t <= 0.0 {
            0
        } else {
            t as usize
        };
        counts[i.min(bins - 1)] += 1.0;
    }
    if cumulative {
        for i in 1..bins {
            counts[i] += counts[i - 1];
        }
    }
    if density {
        let div = if cumulative {
            values.len() as f64
        } else {
            values.len() as f64 * w
        };
        counts.iter_mut().for_each(|c| *c /= div);
    }
    (lo, hi, counts)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fit_range_linear_and_log() {
        assert_eq!(
            fit_range(0.0, 10.0, Some(1.0), Scale::Linear),
            Some((-0.5, 10.5))
        );
        assert_eq!(fit_range(3.0, 3.0, None, Scale::Linear), Some((2.5, 3.5)));
        let (lo, hi) = fit_range(-5.0, 100.0, Some(1.0), Scale::Log).unwrap();
        assert!(lo < 1.0 && lo > 0.0 && hi > 100.0);
        assert_eq!(fit_range(-5.0, -1.0, None, Scale::Log), None);
        assert_eq!(fit_range(0.0, 10.0, None, Scale::Sqrt).unwrap().0, 0.0);
    }

    #[test]
    fn sticky_baseline() {
        let r = stick((-0.5, 10.5), 0.0, 10.0, Some((0.0, 0.0)), Scale::Linear);
        assert_eq!(r, (0.0, 10.5));
    }

    #[test]
    fn stacked_extent() {
        let a = BarRec {
            pos: vec![0.0, 1.0],
            heights: vec![1.0, 2.0],
            bottom: 0.0,
            horizontal: false,
            width: 0.8,
        };
        let b = BarRec {
            pos: vec![0.0, 1.0],
            heights: vec![3.0, -1.0],
            bottom: 0.0,
            horizontal: false,
            width: 0.8,
        };
        let (ext, origins) = bar_extent(&[&a, &b], BarMode::Stacked);
        assert_eq!(ext.iter().copied().fold(f64::MIN, f64::max), 4.0);
        assert_eq!(ext.iter().copied().fold(f64::MAX, f64::min), -1.0);
        assert_eq!(origins, vec![0.0]);
        let (ext, _) = bar_extent(&[&a, &b], BarMode::Grouped);
        assert_eq!(ext.iter().copied().fold(f64::MIN, f64::max), 3.0);
    }

    #[test]
    fn box_stats_match_core() {
        let s = box_stats(&[1.0, 2.0, 3.0, 4.0, 100.0]).unwrap();
        assert_eq!(s.median, 3.0);
        assert_eq!(s.q1, 2.0);
        assert_eq!(s.q3, 4.0);
        assert_eq!(s.whisker_high, 4.0);
        assert_eq!(s.outliers, vec![100.0]);
        assert_eq!(s.count, 5);
        assert!(box_stats(&[f64::NAN]).is_none());
    }

    #[test]
    fn histogram_counts_cover_all_values() {
        let (lo, hi, c) = histogram_counts(&[0.0, 1.0, 1.0, 2.0], 2, false, false);
        assert_eq!((lo, hi), (0.0, 2.0));
        assert_eq!(c, vec![1.0, 3.0]);
        let (_, _, c) = histogram_counts(&[0.0, 1.0, 1.0, 2.0], 2, false, true);
        assert_eq!(c, vec![1.0, 4.0]);
    }

    #[test]
    fn error_spec_expand() {
        let (l, h) = ErrorSpec::Scalar(-0.5).expand(2, "yerr").unwrap();
        assert_eq!((l, h), (vec![0.5, 0.5], vec![0.5, 0.5]));
        assert!(ErrorSpec::Symmetric(vec![1.0]).expand(2, "yerr").is_err());
    }
}
