//! Colour parsing: MATLAB letters, palette slots, names, hex and RGB(A).

use std::fmt;
use std::str::FromStr;

use crate::error::{Error, Result};

/// An RGBA colour with components in `0.0..=1.0`.
#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub struct Color {
    /// Red.
    pub r: f32,
    /// Green.
    pub g: f32,
    /// Blue.
    pub b: f32,
    /// Alpha (1 = opaque).
    pub a: f32,
}

impl Color {
    /// An opaque colour.
    pub const fn rgb(r: f32, g: f32, b: f32) -> Color {
        Color { r, g, b, a: 1.0 }
    }

    /// A colour with alpha.
    pub const fn rgba(r: f32, g: f32, b: f32, a: f32) -> Color {
        Color { r, g, b, a }
    }

    /// The same colour with a different alpha.
    pub const fn with_alpha(self, a: f32) -> Color {
        Color { a, ..self }
    }

    /// `0xRRGGBB` as an opaque colour.
    pub fn from_hex_u32(v: u32) -> Color {
        Color::rgb(
            ((v >> 16) & 0xff) as f32 / 255.0,
            ((v >> 8) & 0xff) as f32 / 255.0,
            (v & 0xff) as f32 / 255.0,
        )
    }

    /// Parse `#rgb`, `#rgba`, `#rrggbb` or `#rrggbbaa`.
    pub fn from_hex(text: &str) -> Option<Color> {
        let h = text.trim().strip_prefix('#')?;
        if !h.chars().all(|c| c.is_ascii_hexdigit()) {
            return None;
        }
        let full: String = match h.len() {
            3 | 4 => h.chars().flat_map(|c| [c, c]).collect(),
            6 | 8 => h.to_string(),
            _ => return None,
        };
        let byte = |i: usize| {
            u8::from_str_radix(&full[i..i + 2], 16)
                .ok()
                .map(|b| b as f32 / 255.0)
        };
        let a = if full.len() == 8 { byte(6)? } else { 1.0 };
        Some(Color::rgba(byte(0)?, byte(2)?, byte(4)?, a))
    }

    pub(crate) fn to_array(self) -> [f32; 4] {
        [self.r, self.g, self.b, self.a]
    }

    fn validate(self) -> Result<Color> {
        if [self.r, self.g, self.b, self.a]
            .iter()
            .all(|v| v.is_finite() && (0.0..=1.0).contains(v))
        {
            Ok(self)
        } else {
            Err(Error::invalid(format!(
                "colour components must be in [0, 1], got {self:?}"
            )))
        }
    }
}

impl From<(f32, f32, f32)> for Color {
    fn from((r, g, b): (f32, f32, f32)) -> Color {
        Color::rgb(r, g, b)
    }
}

impl From<(f32, f32, f32, f32)> for Color {
    fn from((r, g, b, a): (f32, f32, f32, f32)) -> Color {
        Color::rgba(r, g, b, a)
    }
}

impl From<[f32; 3]> for Color {
    fn from([r, g, b]: [f32; 3]) -> Color {
        Color::rgb(r, g, b)
    }
}

impl From<[f32; 4]> for Color {
    fn from([r, g, b, a]: [f32; 4]) -> Color {
        Color::rgba(r, g, b, a)
    }
}

/// How a series is coloured.
///
/// Letters and palette slots are resolved by the window for its theme (`'k'`
/// is black on a light plot and light grey on a dark one) and follow theme
/// changes; [`ColorSpec::Rgba`] is a fixed colour.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum ColorSpec {
    /// MATLAB colour letter: one of `r g b c m y k w`.
    Letter(char),
    /// Slot `n` of the data palette (`"C0"`, `"C1"`, ...).
    Palette(u32),
    /// A fixed colour.
    Rgba(Color),
}

impl ColorSpec {
    /// Parse a colour name: a letter or its name (`"r"`, `"red"`), `"C3"`,
    /// `"tab:blue"`, `"#ff8800"`, a CSS name (`"steelblue"`), a grey level
    /// (`"0.5"`) or `"none"`.
    pub fn parse(text: &str) -> Result<ColorSpec> {
        let lower = text.trim().to_ascii_lowercase();
        if lower.is_empty() {
            return Err(Error::invalid("empty colour string"));
        }
        if let Some(letter) = letter_for_name(&lower) {
            return Ok(ColorSpec::Letter(letter));
        }
        if let Some(n) = lower.strip_prefix('c').and_then(|d| d.parse::<u32>().ok()) {
            if lower.len() <= 6 {
                return Ok(ColorSpec::Palette(n));
            }
        }
        Ok(ColorSpec::Rgba(parse_rgba(&lower, text)?))
    }

    /// The colour as RGBA (letters use Spectra's tuned shades, palette slots
    /// the default palette).
    pub fn to_color(self) -> Color {
        match self {
            ColorSpec::Letter(c) => named_rgba(&c.to_string()).unwrap_or_default(),
            ColorSpec::Palette(n) => {
                Color::from_hex_u32(DEFAULT_CYCLE[n as usize % DEFAULT_CYCLE.len()])
            }
            ColorSpec::Rgba(c) => c,
        }
    }
}

impl FromStr for ColorSpec {
    type Err = Error;
    fn from_str(s: &str) -> Result<Self> {
        ColorSpec::parse(s)
    }
}

impl FromStr for Color {
    type Err = Error;
    fn from_str(s: &str) -> Result<Self> {
        Ok(ColorSpec::parse(s)?.to_color())
    }
}

impl fmt::Display for Color {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let b = |v: f32| (v.clamp(0.0, 1.0) * 255.0).round() as u8;
        write!(
            f,
            "#{:02x}{:02x}{:02x}{:02x}",
            b(self.r),
            b(self.g),
            b(self.b),
            b(self.a)
        )
    }
}

/// Anything accepted where a colour is expected.
pub trait IntoColor {
    /// Convert, validating names and ranges.
    fn into_color_spec(self) -> Result<ColorSpec>;
}

impl IntoColor for ColorSpec {
    fn into_color_spec(self) -> Result<ColorSpec> {
        if let ColorSpec::Rgba(c) = self {
            c.validate()?;
        }
        Ok(self)
    }
}

impl IntoColor for Color {
    fn into_color_spec(self) -> Result<ColorSpec> {
        Ok(ColorSpec::Rgba(self.validate()?))
    }
}

impl IntoColor for &str {
    fn into_color_spec(self) -> Result<ColorSpec> {
        ColorSpec::parse(self)
    }
}

impl IntoColor for String {
    fn into_color_spec(self) -> Result<ColorSpec> {
        ColorSpec::parse(&self)
    }
}

impl IntoColor for &String {
    fn into_color_spec(self) -> Result<ColorSpec> {
        ColorSpec::parse(self)
    }
}

impl IntoColor for char {
    fn into_color_spec(self) -> Result<ColorSpec> {
        ColorSpec::parse(&self.to_string())
    }
}

impl IntoColor for (f32, f32, f32) {
    fn into_color_spec(self) -> Result<ColorSpec> {
        Color::from(self).into_color_spec()
    }
}

impl IntoColor for (f32, f32, f32, f32) {
    fn into_color_spec(self) -> Result<ColorSpec> {
        Color::from(self).into_color_spec()
    }
}

impl IntoColor for [f32; 3] {
    fn into_color_spec(self) -> Result<ColorSpec> {
        Color::from(self).into_color_spec()
    }
}

impl IntoColor for [f32; 4] {
    fn into_color_spec(self) -> Result<ColorSpec> {
        Color::from(self).into_color_spec()
    }
}

/// Spectra's default data palette (`palette::default_cycle`).
pub(crate) const DEFAULT_CYCLE: [u32; 10] = [
    0x22D3EE, 0xFB923C, 0xE879F9, 0xA3E635, 0x60A5FA, 0xFB7185, 0xFACC15, 0x34D399, 0xC084FC,
    0xE5E7EB,
];

fn letter_for_name(lower: &str) -> Option<char> {
    Some(match lower {
        "r" | "red" => 'r',
        "g" | "green" => 'g',
        "b" | "blue" => 'b',
        "c" | "cyan" => 'c',
        "m" | "magenta" => 'm',
        "y" | "yellow" => 'y',
        "k" | "black" => 'k',
        "w" | "white" => 'w',
        _ => return None,
    })
}

/// Spectra's own shades for the short names (take precedence over CSS4).
fn named_rgba(lower: &str) -> Option<Color> {
    let c = |r, g, b| Some(Color::rgb(r, g, b));
    match lower {
        "red" | "r" => c(1.0, 0.2, 0.2),
        "green" | "g" => c(0.2, 0.8, 0.2),
        "blue" | "b" => c(0.2, 0.4, 1.0),
        "yellow" | "y" => c(1.0, 0.9, 0.1),
        "cyan" | "c" => c(0.0, 0.9, 0.9),
        "magenta" | "m" => c(0.9, 0.1, 0.9),
        "white" | "w" => c(1.0, 1.0, 1.0),
        "black" | "k" => c(0.0, 0.0, 0.0),
        "orange" => c(1.0, 0.5, 0.0),
        "purple" => c(0.6, 0.2, 0.9),
        "pink" => c(1.0, 0.4, 0.7),
        "gray" | "grey" => c(0.5, 0.5, 0.5),
        _ => None,
    }
}

const TABLEAU: [(&str, u32); 11] = [
    ("tab:blue", 0x1f77b4),
    ("tab:orange", 0xff7f0e),
    ("tab:green", 0x2ca02c),
    ("tab:red", 0xd62728),
    ("tab:purple", 0x9467bd),
    ("tab:brown", 0x8c564b),
    ("tab:pink", 0xe377c2),
    ("tab:gray", 0x7f7f7f),
    ("tab:grey", 0x7f7f7f),
    ("tab:olive", 0xbcbd22),
    ("tab:cyan", 0x17becf),
];

fn parse_rgba(lower: &str, original: &str) -> Result<Color> {
    if let Some(c) = named_rgba(lower) {
        return Ok(c);
    }
    if lower == "none" {
        return Ok(Color::rgba(0.0, 0.0, 0.0, 0.0));
    }
    if let Some((_, v)) = TABLEAU.iter().find(|(n, _)| *n == lower) {
        return Ok(Color::from_hex_u32(*v));
    }
    if let Ok(i) = CSS4.binary_search_by(|(n, _)| n.cmp(&lower)) {
        return Ok(Color::from_hex_u32(CSS4[i].1));
    }
    if lower.starts_with('#') {
        return Color::from_hex(lower).ok_or_else(|| {
            Error::invalid(format!(
                "invalid hex colour {original:?}: use #rgb, #rgba, #rrggbb or #rrggbbaa"
            ))
        });
    }
    if let Ok(level) = lower.parse::<f32>() {
        if (0.0..=1.0).contains(&level) {
            return Ok(Color::rgb(level, level, level));
        }
        return Err(Error::invalid(format!(
            "grey level {original:?} must be between 0 and 1"
        )));
    }
    Err(Error::invalid(format!("unknown colour {original:?}")))
}

/// CSS Color Module Level 4 named colours, sorted by name.
const CSS4: [(&str, u32); 148] = [
    ("aliceblue", 0xf0f8ff),
    ("antiquewhite", 0xfaebd7),
    ("aqua", 0x00ffff),
    ("aquamarine", 0x7fffd4),
    ("azure", 0xf0ffff),
    ("beige", 0xf5f5dc),
    ("bisque", 0xffe4c4),
    ("black", 0x000000),
    ("blanchedalmond", 0xffebcd),
    ("blue", 0x0000ff),
    ("blueviolet", 0x8a2be2),
    ("brown", 0xa52a2a),
    ("burlywood", 0xdeb887),
    ("cadetblue", 0x5f9ea0),
    ("chartreuse", 0x7fff00),
    ("chocolate", 0xd2691e),
    ("coral", 0xff7f50),
    ("cornflowerblue", 0x6495ed),
    ("cornsilk", 0xfff8dc),
    ("crimson", 0xdc143c),
    ("cyan", 0x00ffff),
    ("darkblue", 0x00008b),
    ("darkcyan", 0x008b8b),
    ("darkgoldenrod", 0xb8860b),
    ("darkgray", 0xa9a9a9),
    ("darkgreen", 0x006400),
    ("darkgrey", 0xa9a9a9),
    ("darkkhaki", 0xbdb76b),
    ("darkmagenta", 0x8b008b),
    ("darkolivegreen", 0x556b2f),
    ("darkorange", 0xff8c00),
    ("darkorchid", 0x9932cc),
    ("darkred", 0x8b0000),
    ("darksalmon", 0xe9967a),
    ("darkseagreen", 0x8fbc8f),
    ("darkslateblue", 0x483d8b),
    ("darkslategray", 0x2f4f4f),
    ("darkslategrey", 0x2f4f4f),
    ("darkturquoise", 0x00ced1),
    ("darkviolet", 0x9400d3),
    ("deeppink", 0xff1493),
    ("deepskyblue", 0x00bfff),
    ("dimgray", 0x696969),
    ("dimgrey", 0x696969),
    ("dodgerblue", 0x1e90ff),
    ("firebrick", 0xb22222),
    ("floralwhite", 0xfffaf0),
    ("forestgreen", 0x228b22),
    ("fuchsia", 0xff00ff),
    ("gainsboro", 0xdcdcdc),
    ("ghostwhite", 0xf8f8ff),
    ("gold", 0xffd700),
    ("goldenrod", 0xdaa520),
    ("gray", 0x808080),
    ("green", 0x008000),
    ("greenyellow", 0xadff2f),
    ("grey", 0x808080),
    ("honeydew", 0xf0fff0),
    ("hotpink", 0xff69b4),
    ("indianred", 0xcd5c5c),
    ("indigo", 0x4b0082),
    ("ivory", 0xfffff0),
    ("khaki", 0xf0e68c),
    ("lavender", 0xe6e6fa),
    ("lavenderblush", 0xfff0f5),
    ("lawngreen", 0x7cfc00),
    ("lemonchiffon", 0xfffacd),
    ("lightblue", 0xadd8e6),
    ("lightcoral", 0xf08080),
    ("lightcyan", 0xe0ffff),
    ("lightgoldenrodyellow", 0xfafad2),
    ("lightgray", 0xd3d3d3),
    ("lightgreen", 0x90ee90),
    ("lightgrey", 0xd3d3d3),
    ("lightpink", 0xffb6c1),
    ("lightsalmon", 0xffa07a),
    ("lightseagreen", 0x20b2aa),
    ("lightskyblue", 0x87cefa),
    ("lightslategray", 0x778899),
    ("lightslategrey", 0x778899),
    ("lightsteelblue", 0xb0c4de),
    ("lightyellow", 0xffffe0),
    ("lime", 0x00ff00),
    ("limegreen", 0x32cd32),
    ("linen", 0xfaf0e6),
    ("magenta", 0xff00ff),
    ("maroon", 0x800000),
    ("mediumaquamarine", 0x66cdaa),
    ("mediumblue", 0x0000cd),
    ("mediumorchid", 0xba55d3),
    ("mediumpurple", 0x9370db),
    ("mediumseagreen", 0x3cb371),
    ("mediumslateblue", 0x7b68ee),
    ("mediumspringgreen", 0x00fa9a),
    ("mediumturquoise", 0x48d1cc),
    ("mediumvioletred", 0xc71585),
    ("midnightblue", 0x191970),
    ("mintcream", 0xf5fffa),
    ("mistyrose", 0xffe4e1),
    ("moccasin", 0xffe4b5),
    ("navajowhite", 0xffdead),
    ("navy", 0x000080),
    ("oldlace", 0xfdf5e6),
    ("olive", 0x808000),
    ("olivedrab", 0x6b8e23),
    ("orange", 0xffa500),
    ("orangered", 0xff4500),
    ("orchid", 0xda70d6),
    ("palegoldenrod", 0xeee8aa),
    ("palegreen", 0x98fb98),
    ("paleturquoise", 0xafeeee),
    ("palevioletred", 0xdb7093),
    ("papayawhip", 0xffefd5),
    ("peachpuff", 0xffdab9),
    ("peru", 0xcd853f),
    ("pink", 0xffc0cb),
    ("plum", 0xdda0dd),
    ("powderblue", 0xb0e0e6),
    ("purple", 0x800080),
    ("rebeccapurple", 0x663399),
    ("red", 0xff0000),
    ("rosybrown", 0xbc8f8f),
    ("royalblue", 0x4169e1),
    ("saddlebrown", 0x8b4513),
    ("salmon", 0xfa8072),
    ("sandybrown", 0xf4a460),
    ("seagreen", 0x2e8b57),
    ("seashell", 0xfff5ee),
    ("sienna", 0xa0522d),
    ("silver", 0xc0c0c0),
    ("skyblue", 0x87ceeb),
    ("slateblue", 0x6a5acd),
    ("slategray", 0x708090),
    ("slategrey", 0x708090),
    ("snow", 0xfffafa),
    ("springgreen", 0x00ff7f),
    ("steelblue", 0x4682b4),
    ("tan", 0xd2b48c),
    ("teal", 0x008080),
    ("thistle", 0xd8bfd8),
    ("tomato", 0xff6347),
    ("turquoise", 0x40e0d0),
    ("violet", 0xee82ee),
    ("wheat", 0xf5deb3),
    ("white", 0xffffff),
    ("whitesmoke", 0xf5f5f5),
    ("yellow", 0xffff00),
    ("yellowgreen", 0x9acd32),
];

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_specs() {
        assert_eq!(ColorSpec::parse("r").unwrap(), ColorSpec::Letter('r'));
        assert_eq!(ColorSpec::parse("Black").unwrap(), ColorSpec::Letter('k'));
        assert_eq!(ColorSpec::parse("C3").unwrap(), ColorSpec::Palette(3));
        assert_eq!(
            ColorSpec::parse("#ff0000").unwrap(),
            ColorSpec::Rgba(Color::rgb(1.0, 0.0, 0.0))
        );
        assert_eq!(
            ColorSpec::parse("steelblue").unwrap(),
            ColorSpec::Rgba(Color::from_hex_u32(0x4682b4))
        );
        assert_eq!(
            ColorSpec::parse("0.5").unwrap(),
            ColorSpec::Rgba(Color::rgb(0.5, 0.5, 0.5))
        );
        assert_eq!(
            ColorSpec::parse("tab:red").unwrap(),
            ColorSpec::Rgba(Color::from_hex_u32(0xd62728))
        );
        assert!(ColorSpec::parse("blurple").is_err());
        assert!(ColorSpec::parse("#12345").is_err());
        assert!(ColorSpec::parse("1.5").is_err());
    }

    #[test]
    fn css_table_is_sorted() {
        assert!(CSS4.windows(2).all(|w| w[0].0 < w[1].0));
    }

    #[test]
    fn short_hex_and_alpha() {
        assert_eq!(Color::from_hex("#f00").unwrap(), Color::rgb(1.0, 0.0, 0.0));
        let c = Color::from_hex("#00000080").unwrap();
        assert!((c.a - 128.0 / 255.0).abs() < 1e-6);
    }

    #[test]
    fn into_color_validates_range() {
        assert!((1.5f32, 0.0f32, 0.0f32).into_color_spec().is_err());
        assert!((0.5f32, 0.0f32, 0.0f32).into_color_spec().is_ok());
    }
}
