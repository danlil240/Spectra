//! Text annotations on 2D axes.

use std::sync::{Arc, Mutex};

use crate::axes::Axes;
use crate::codec::PropertyUpdate;
use crate::color::{Color, IntoColor};
use crate::error::{Error, Result};
use crate::protocol as p;
use crate::session::lock;
use crate::style::{HAlign, VAlign};

/// Background box behind annotation text.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct TextBox {
    /// Padding around the text in pixels.
    pub pad: f32,
    /// Fill colour (`None` = theme default).
    pub face: Option<Color>,
    /// Border colour (`None` = theme default).
    pub edge: Option<Color>,
    /// Border width in pixels (0 = no border).
    pub edge_width: f32,
}

impl Default for TextBox {
    fn default() -> Self {
        TextBox {
            pad: 4.0,
            face: None,
            edge: None,
            edge_width: 1.0,
        }
    }
}

/// Glyph outline drawn around annotation text so it reads over data.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Halo {
    /// Outline colour (`None` = the theme background).
    pub color: Option<Color>,
    /// Outline width in pixels.
    pub width: f32,
}

impl Default for Halo {
    fn default() -> Self {
        Halo {
            color: None,
            width: 2.0,
        }
    }
}

/// Options for [`Axes::text_with`] and [`Axes::annotate_with`].
#[derive(Debug, Clone, PartialEq)]
pub struct TextOptions {
    /// Text colour (`None` = theme text colour).
    pub color: Option<Color>,
    /// Font size in pixels (0 = axis label size).
    pub font_size: f32,
    /// Horizontal alignment relative to the anchor.
    pub halign: HAlign,
    /// Vertical alignment relative to the anchor.
    pub valign: VAlign,
    /// Background box.
    pub bbox: Option<TextBox>,
    /// Glyph outline.
    pub halo: Option<Halo>,
    /// Rotation in degrees, counter-clockwise about the anchor.
    pub rotation: f32,
}

impl Default for TextOptions {
    fn default() -> Self {
        TextOptions {
            color: None,
            font_size: 0.0,
            halign: HAlign::Left,
            valign: VAlign::Bottom,
            bbox: None,
            halo: None,
            rotation: 0.0,
        }
    }
}

impl TextOptions {
    /// Set the text colour from any colour value.
    pub fn color(mut self, color: impl IntoColor) -> Result<Self> {
        self.color = Some(color.into_color_spec()?.to_color());
        Ok(self)
    }
}

#[derive(Debug, Clone)]
pub(crate) struct AnnotationState {
    /// Position within the axes' annotation list; -1 once cleared.
    pub index: i64,
    pub x: f64,
    pub y: f64,
    pub text: String,
    pub opts: TextOptions,
    pub arrow: Option<(f64, f64)>,
    pub visible: bool,
}

fn rgba(c: Option<Color>) -> [f32; 5] {
    match c {
        Some(c) => [1.0, c.r, c.g, c.b, c.a],
        None => [0.0, 0.0, 0.0, 0.0, 1.0],
    }
}

impl AnnotationState {
    /// Packed like `spectra::ipc::pack_annotation()` (src/ipc/annotation_state.hpp).
    pub fn packed(&self) -> Vec<f32> {
        let o = &self.opts;
        let (ax, ay) = self.arrow.unwrap_or((0.0, 0.0));
        let mut v = vec![self.x as f32, self.y as f32];
        v.extend_from_slice(&rgba(o.color));
        v.extend_from_slice(&[
            o.font_size,
            o.halign.wire() as f32,
            o.valign.wire() as f32,
            if self.arrow.is_some() { 1.0 } else { 0.0 },
            ax as f32,
            ay as f32,
            if self.visible { 1.0 } else { 0.0 },
        ]);
        if o.bbox.is_none() && o.halo.is_none() && o.rotation == 0.0 {
            return v;
        }
        let b = o.bbox.unwrap_or_default();
        v.extend_from_slice(&[if o.bbox.is_some() { 1.0 } else { 0.0 }, b.pad]);
        v.extend_from_slice(&rgba(b.face));
        v.extend_from_slice(&rgba(b.edge));
        let h = o.halo.unwrap_or_default();
        v.extend_from_slice(&[b.edge_width, if o.halo.is_some() { 1.0 } else { 0.0 }]);
        v.extend_from_slice(&rgba(h.color));
        v.extend_from_slice(&[h.width, o.rotation]);
        v
    }
}

/// A text annotation on a 2D axes. Setters resend the whole annotation.
#[derive(Clone)]
pub struct TextAnnotation {
    pub(crate) axes: Axes,
    pub(crate) state: Arc<Mutex<AnnotationState>>,
}

impl std::fmt::Debug for TextAnnotation {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let s = lock(&self.state);
        f.debug_struct("TextAnnotation")
            .field("index", &s.index)
            .field("text", &s.text)
            .field("x", &s.x)
            .field("y", &s.y)
            .finish()
    }
}

impl TextAnnotation {
    pub(crate) fn send(&self, append: bool) -> Result<()> {
        let s = lock(&self.state).clone();
        if s.index < 0 {
            return Err(Error::invalid(
                "annotation was removed by clear_annotations()",
            ));
        }
        let idx = if append {
            p::ANNOTATION_APPEND
        } else {
            s.index as u32
        };
        self.axes.session().update(
            PropertyUpdate::new(self.axes.figure_id(), "annotation")
                .axes(self.axes.index())
                .series(idx)
                .s(s.text.clone())
                .data(s.packed()),
        )
    }

    fn modify(&self, f: impl FnOnce(&mut AnnotationState)) -> Result<&Self> {
        f(&mut lock(&self.state));
        self.send(false)?;
        Ok(self)
    }

    /// Position in the axes' annotation list.
    pub fn index(&self) -> i64 {
        lock(&self.state).index
    }

    /// Current text.
    pub fn text(&self) -> String {
        lock(&self.state).text.clone()
    }

    /// Anchor position in data coordinates.
    pub fn position(&self) -> (f64, f64) {
        let s = lock(&self.state);
        (s.x, s.y)
    }

    /// Replace the text.
    pub fn set_text(&self, text: &str) -> Result<&Self> {
        self.modify(|s| s.text = text.to_string())
    }

    /// Move the anchor.
    pub fn set_position(&self, x: f64, y: f64) -> Result<&Self> {
        self.modify(|s| {
            s.x = x;
            s.y = y;
        })
    }

    /// Text colour.
    pub fn set_color(&self, color: impl IntoColor) -> Result<&Self> {
        let c = color.into_color_spec()?.to_color();
        self.modify(|s| s.opts.color = Some(c))
    }

    /// Font size in pixels (0 = axis label size).
    pub fn set_font_size(&self, px: f32) -> Result<&Self> {
        self.modify(|s| s.opts.font_size = px.max(0.0))
    }

    /// Alignment relative to the anchor.
    pub fn set_alignment(&self, halign: HAlign, valign: VAlign) -> Result<&Self> {
        self.modify(|s| {
            s.opts.halign = halign;
            s.opts.valign = valign;
        })
    }

    /// Point an arrow from the text to `(x, y)`; `None` removes it.
    pub fn set_arrow(&self, target: Option<(f64, f64)>) -> Result<&Self> {
        self.modify(|s| s.arrow = target)
    }

    /// Show or hide the annotation.
    pub fn set_visible(&self, visible: bool) -> Result<&Self> {
        self.modify(|s| s.visible = visible)
    }

    /// Background box (`None` removes it).
    pub fn set_bbox(&self, bbox: Option<TextBox>) -> Result<&Self> {
        self.modify(|s| s.opts.bbox = bbox)
    }

    /// Glyph outline (`None` removes it).
    pub fn set_halo(&self, halo: Option<Halo>) -> Result<&Self> {
        self.modify(|s| s.opts.halo = halo)
    }

    /// Rotation in degrees, counter-clockwise about the anchor.
    pub fn set_rotation(&self, degrees: f32) -> Result<&Self> {
        self.modify(|s| s.opts.rotation = degrees)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn state(opts: TextOptions) -> AnnotationState {
        AnnotationState {
            index: 0,
            x: 1.0,
            y: 2.0,
            text: "t".into(),
            opts,
            arrow: None,
            visible: true,
        }
    }

    #[test]
    fn packed_base_layout() {
        let v = state(TextOptions::default()).packed();
        assert_eq!(v.len(), 14);
        assert_eq!(&v[..3], &[1.0, 2.0, 0.0]);
        assert_eq!(v[9], 2.0); // valign bottom
        assert_eq!(v[13], 1.0); // visible
    }

    #[test]
    fn packed_style_tail() {
        let o = TextOptions {
            bbox: Some(TextBox::default()),
            rotation: 30.0,
            ..Default::default()
        };
        let v = state(o).packed();
        // 14 base + box(2) + face(5) + edge(5) + edge_w + halo flag + halo colour(5) + width + rotation
        assert_eq!(v.len(), 14 + 2 + 5 + 5 + 1 + 1 + 5 + 1 + 1);
        assert_eq!(*v.last().unwrap(), 30.0);
    }
}
