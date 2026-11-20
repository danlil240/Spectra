//! Line styles, markers, MATLAB-style format strings and other style enums.

use std::fmt;
use std::str::FromStr;

use crate::error::{Error, Result};

/// How a line is dashed.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum LineStyle {
    /// `"-"`
    Solid,
    /// `"--"`
    Dashed,
    /// `":"`
    Dotted,
    /// `"-."`
    DashDot,
    /// `"-.."`
    DashDotDot,
    /// No line (markers only).
    None,
}

impl LineStyle {
    /// Backend token for the `line_style` property.
    pub fn token(self) -> &'static str {
        match self {
            LineStyle::Solid => "-",
            LineStyle::Dashed => "--",
            LineStyle::Dotted => ":",
            LineStyle::DashDot => "-.",
            LineStyle::DashDotDot => "-..",
            LineStyle::None => "none",
        }
    }
}

impl FromStr for LineStyle {
    type Err = Error;
    fn from_str(s: &str) -> Result<Self> {
        Ok(match s.trim() {
            "-" => LineStyle::Solid,
            "--" => LineStyle::Dashed,
            ":" => LineStyle::Dotted,
            "-." => LineStyle::DashDot,
            "-.." => LineStyle::DashDotDot,
            "" => LineStyle::None,
            other => match other.to_ascii_lowercase().as_str() {
                "solid" => LineStyle::Solid,
                "dashed" => LineStyle::Dashed,
                "dotted" => LineStyle::Dotted,
                "dashdot" => LineStyle::DashDot,
                "dashdotdot" => LineStyle::DashDotDot,
                "none" => LineStyle::None,
                _ => {
                    return Err(Error::invalid(format!(
                        "unknown line style {s:?}; use '-', '--', ':', '-.', '-..' or 'none'"
                    )))
                }
            },
        })
    }
}

/// Marker drawn at each data point.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Marker {
    /// `"."`
    Point,
    /// `"o"`
    Circle,
    /// `"+"`
    Plus,
    /// `"x"`
    Cross,
    /// `"*"`
    Star,
    /// `"s"`
    Square,
    /// `"d"`
    Diamond,
    /// `"D"` (filled diamond)
    FilledDiamond,
    /// `"^"`
    TriangleUp,
    /// `"v"`
    TriangleDown,
    /// `"<"`
    TriangleLeft,
    /// `">"`
    TriangleRight,
    /// `"p"`
    Pentagon,
    /// `"h"`
    Hexagon,
    /// `"O"` (filled circle)
    FilledCircle,
    /// `"S"` (filled square)
    FilledSquare,
    /// `"A"` (filled triangle)
    FilledTriangle,
    /// No marker.
    None,
}

impl Marker {
    /// Backend token for the `marker_style` property.
    pub fn token(self) -> &'static str {
        match self {
            Marker::Point => ".",
            Marker::Circle => "o",
            Marker::Plus => "+",
            Marker::Cross => "x",
            Marker::Star => "*",
            Marker::Square => "s",
            Marker::Diamond => "d",
            Marker::FilledDiamond => "D",
            Marker::TriangleUp => "^",
            Marker::TriangleDown => "v",
            Marker::TriangleLeft => "<",
            Marker::TriangleRight => ">",
            Marker::Pentagon => "p",
            Marker::Hexagon => "h",
            Marker::FilledCircle => "O",
            Marker::FilledSquare => "S",
            Marker::FilledTriangle => "A",
            Marker::None => "none",
        }
    }

    fn from_symbol(c: char) -> Option<Marker> {
        Some(match c {
            '.' | ',' => Marker::Point,
            'o' => Marker::Circle,
            '+' => Marker::Plus,
            'x' | 'X' => Marker::Cross,
            '*' => Marker::Star,
            's' => Marker::Square,
            'd' => Marker::Diamond,
            'D' => Marker::FilledDiamond,
            '^' => Marker::TriangleUp,
            'v' => Marker::TriangleDown,
            '<' => Marker::TriangleLeft,
            '>' => Marker::TriangleRight,
            'p' => Marker::Pentagon,
            'h' | 'H' => Marker::Hexagon,
            'O' => Marker::FilledCircle,
            'S' => Marker::FilledSquare,
            'A' => Marker::FilledTriangle,
            _ => return None,
        })
    }
}

impl FromStr for Marker {
    type Err = Error;
    fn from_str(s: &str) -> Result<Self> {
        let mut chars = s.chars();
        if let (Some(c), None) = (chars.next(), chars.next()) {
            if let Some(m) = Marker::from_symbol(c) {
                return Ok(m);
            }
        }
        Ok(match s.trim().to_ascii_lowercase().as_str() {
            "circle" => Marker::Circle,
            "square" => Marker::Square,
            "diamond" => Marker::Diamond,
            "triangle" => Marker::TriangleUp,
            "point" => Marker::Point,
            "plus" => Marker::Plus,
            "cross" => Marker::Cross,
            "star" => Marker::Star,
            "pentagon" => Marker::Pentagon,
            "hexagon" => Marker::Hexagon,
            "none" | "" => Marker::None,
            _ => {
                return Err(Error::invalid(format!(
                    "unknown marker {s:?}; use one of . o + x * s d D ^ v < > p h O S A or 'none'"
                )))
            }
        })
    }
}

/// A parsed MATLAB / matplotlib format string such as `"r--o"`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Format {
    /// Colour letter (`b g r c m y k w`).
    pub color: Option<char>,
    /// Marker symbol.
    pub marker: Option<Marker>,
    /// Line style.
    pub line: Option<LineStyle>,
}

impl Format {
    /// Parse `"[colour][marker][line]"` in any order, like MATLAB and matplotlib.
    ///
    /// `"r--o"` is a red dashed line with circle markers, `"g^"` green
    /// triangles with no line, `"k-."` a black dash-dot line.
    pub fn parse(fmt: &str) -> Result<Format> {
        let mut out = Format::default();
        let mut rest = fmt.trim();
        if rest.is_empty() {
            return Ok(out);
        }
        let bad = || {
            Error::invalid(format!(
                "unrecognised format string {fmt:?}: expected [colour][marker][line], e.g. 'r--', \
                 'bo', 'g^-', 'k-.' (colours: b g r c m y k w)"
            ))
        };
        while !rest.is_empty() {
            if let Some(ls) = ["-..", "--", "-.", "-", ":"]
                .iter()
                .find(|ls| rest.starts_with(**ls))
            {
                if out.line.is_some() {
                    return Err(Error::invalid(format!(
                        "format string {fmt:?} has two line styles"
                    )));
                }
                out.line = Some(ls.parse()?);
                rest = &rest[ls.len()..];
                continue;
            }
            let c = rest.chars().next().ok_or_else(bad)?;
            rest = &rest[c.len_utf8()..];
            if "bgrcmykw".contains(c) && out.color.is_none() {
                out.color = Some(c);
            } else if let (Some(m), None) = (Marker::from_symbol(c), out.marker) {
                out.marker = Some(m);
            } else {
                return Err(bad());
            }
        }
        // A marker without a line style draws markers only.
        if out.marker.is_some() && out.line.is_none() {
            out.line = Some(LineStyle::None);
        }
        Ok(out)
    }
}

impl FromStr for Format {
    type Err = Error;
    fn from_str(s: &str) -> Result<Self> {
        Format::parse(s)
    }
}

macro_rules! wire_enum {
    ($(#[$m:meta])* $name:ident { $($(#[$vm:meta])* $v:ident = $n:expr => [$($s:literal),+]),+ $(,)? }) => {
        $(#[$m])*
        #[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
        pub enum $name { $($(#[$vm])* $v),+ }

        impl $name {
            pub(crate) fn wire(self) -> u8 {
                match self { $($name::$v => $n),+ }
            }
        }

        impl FromStr for $name {
            type Err = Error;
            fn from_str(s: &str) -> Result<Self> {
                match s.trim().to_ascii_lowercase().as_str() {
                    $($($s)|+ => Ok($name::$v),)+
                    _ => Err(Error::invalid(format!(
                        concat!("invalid ", stringify!($name), " {:?}; expected one of: {}"),
                        s,
                        [$($($s),+),+].join(", ")
                    ))),
                }
            }
        }
    };
}

wire_enum!(
    /// Axis scale (`spectra::ScaleType`).
    Scale {
        /// Linear (default).
        Linear = 0 => ["linear"],
        /// Base-10 logarithm.
        Log = 1 => ["log", "log10"],
        /// Base-2 logarithm.
        Log2 = 2 => ["log2"],
        /// Square root.
        Sqrt = 3 => ["sqrt"],
    }
);

wire_enum!(
    /// How several bar series at the same positions are drawn (`spectra::BarMode`).
    BarMode {
        /// Side by side (default).
        Grouped = 0 => ["grouped"],
        /// Stacked; negative values stack downwards separately.
        Stacked = 1 => ["stacked"],
        /// Drawn over each other.
        Overlay = 2 => ["overlay"],
    }
);

wire_enum!(
    /// How a line connects its points (`spectra::DrawStyle`).
    DrawStyle {
        /// Straight segments.
        Default = 0 => ["default", "lines"],
        /// Step at the start of each interval.
        StepsPre = 1 => ["steps", "steps-pre", "pre"],
        /// Step in the middle.
        StepsMid = 2 => ["steps-mid", "mid"],
        /// Step at the end.
        StepsPost = 3 => ["steps-post", "post"],
    }
);

wire_enum!(
    /// How a line with far more points than pixels is drawn (`spectra::DenseLineMode`).
    DenseMode {
        /// Min/max per pixel column: same picture from fewer points (default).
        Auto = 0 => ["auto", "minmax"],
        /// Every point.
        Exact = 1 => ["exact", "full"],
        /// Every point translucent, so overlaps show density.
        Density = 2 => ["density"],
    }
);

wire_enum!(
    /// Subplot axis sharing (`spectra::ShareAxes`).
    Share {
        /// Independent axes.
        None = 0 => ["none", "false"],
        /// All subplots share.
        All = 1 => ["all", "true"],
        /// Subplots in a row share.
        Row = 2 => ["row"],
        /// Subplots in a column share.
        Col = 3 => ["col"],
    }
);

wire_enum!(
    /// Horizontal text alignment relative to the anchor.
    HAlign {
        /// Text starts at the anchor.
        Left = 0 => ["left"],
        /// Centred on the anchor.
        Center = 1 => ["center", "centre"],
        /// Text ends at the anchor.
        Right = 2 => ["right"],
    }
);

wire_enum!(
    /// Vertical text alignment relative to the anchor.
    VAlign {
        /// Anchor at the top of the text.
        Top = 0 => ["top"],
        /// Anchor at the middle.
        Middle = 1 => ["middle", "center", "centre"],
        /// Anchor at the bottom (default).
        Bottom = 2 => ["bottom", "baseline"],
    }
);

/// 3D light placement.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum LightMode {
    /// A direction tied to the data axes.
    Fixed,
    /// From the camera; follows it while orbiting.
    Headlight,
}

impl LightMode {
    pub(crate) fn token(self) -> &'static str {
        match self {
            LightMode::Fixed => "fixed",
            LightMode::Headlight => "headlight",
        }
    }
}

/// Built-in plot styles (`spectra::StyleSheet`).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum PlotStyle {
    /// The default look.
    Default,
    /// Print-ready: light background, thin lines.
    Paper,
    /// Large fonts and thick lines for slides.
    Talk,
    /// Few decorations.
    Minimal,
    /// Paper style on a dark background.
    DarkPaper,
}

impl PlotStyle {
    /// Every built-in style.
    pub const ALL: [PlotStyle; 5] = [
        PlotStyle::Default,
        PlotStyle::Paper,
        PlotStyle::Talk,
        PlotStyle::Minimal,
        PlotStyle::DarkPaper,
    ];

    /// Backend name of the style.
    pub fn name(self) -> &'static str {
        match self {
            PlotStyle::Default => "default",
            PlotStyle::Paper => "paper",
            PlotStyle::Talk => "talk",
            PlotStyle::Minimal => "minimal",
            PlotStyle::DarkPaper => "dark-paper",
        }
    }
}

impl fmt::Display for PlotStyle {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(self.name())
    }
}

impl FromStr for PlotStyle {
    type Err = Error;
    fn from_str(s: &str) -> Result<Self> {
        PlotStyle::ALL
            .into_iter()
            .find(|p| p.name() == s.trim())
            .ok_or_else(|| {
                Error::invalid(format!(
                "unknown style {s:?}; available styles: default, paper, talk, minimal, dark-paper"
            ))
            })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_format_strings() {
        let f = Format::parse("r--o").unwrap();
        assert_eq!(f.color, Some('r'));
        assert_eq!(f.line, Some(LineStyle::Dashed));
        assert_eq!(f.marker, Some(Marker::Circle));

        let f = Format::parse("g^").unwrap();
        assert_eq!(f.line, Some(LineStyle::None));
        assert_eq!(f.marker, Some(Marker::TriangleUp));

        let f = Format::parse("k-.").unwrap();
        assert_eq!(f.line, Some(LineStyle::DashDot));
        assert_eq!(f.marker, None);

        assert_eq!(
            Format::parse("-..").unwrap().line,
            Some(LineStyle::DashDotDot)
        );
        assert!(Format::parse("r--:").is_err());
        assert!(Format::parse("q").is_err());
        assert_eq!(Format::parse("").unwrap(), Format::default());
    }

    #[test]
    fn enums_from_str() {
        assert_eq!("log10".parse::<Scale>().unwrap(), Scale::Log);
        assert_eq!("steps-post".parse::<DrawStyle>().unwrap().wire(), 3);
        assert_eq!("stacked".parse::<BarMode>().unwrap(), BarMode::Stacked);
        assert!("bogus".parse::<Scale>().is_err());
        assert_eq!("dashed".parse::<LineStyle>().unwrap(), LineStyle::Dashed);
        assert_eq!("circle".parse::<Marker>().unwrap(), Marker::Circle);
        assert_eq!("D".parse::<Marker>().unwrap(), Marker::FilledDiamond);
        assert_eq!(
            "dark-paper".parse::<PlotStyle>().unwrap(),
            PlotStyle::DarkPaper
        );
    }
}
