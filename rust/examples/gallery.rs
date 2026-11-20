//! Every chart type in one run, saved as PNG files.
//!
//! ```sh
//! cargo run --example gallery -- out_dir
//! ```

use std::f64::consts::PI;

use spectra_plot::*;

fn main() -> Result<()> {
    let out = std::env::args()
        .nth(1)
        .unwrap_or_else(|| "gallery".to_string());
    std::fs::create_dir_all(&out)?;
    let session = Session::new()?;

    // Lines, format strings, annotations.
    let fig = session.figure_sized("Lines", 1200, 800)?;
    let ax = fig.subplot(1, 1, 1)?;
    let x = linspace(0.0, 4.0 * PI, 400);
    let sin: Vec<f64> = x.iter().map(|v| v.sin()).collect();
    let cos: Vec<f64> = x.iter().map(|v| v.cos()).collect();
    ax.plot(&x, &sin, "r-")?.set_label("sin")?;
    ax.plot(&x, &cos, "b--o")?
        .set_label("cos")?
        .set_marker_size(3.0)?;
    ax.hline(0.0)?;
    ax.annotate("peak", (PI / 2.0, 1.0), (3.0, 0.6))?;
    ax.text_with(
        8.5,
        -0.5,
        "rotated",
        &TextOptions {
            rotation: 20.0,
            bbox: Some(TextBox::default()),
            ..Default::default()
        },
    )?;
    ax.set_title("Lines")?
        .set_xlabel("x")?
        .set_ylabel("y")?
        .legend(true)?
        .grid(true)?;
    fig.savefig(format!("{out}/lines.png"))?;

    // Statistics on a 2x3 grid.
    let fig = session.figure_sized("Statistics", 1500, 900)?;
    let axes = fig.subplots(2, 3)?;
    let normal: Vec<f64> = (0..2000)
        .map(|i| {
            let u1 = (i as f64 + 0.5) / 2000.0;
            let u2 = ((i * 7919) % 2000) as f64 / 2000.0 + 1e-3;
            (-2.0 * u2.ln()).sqrt() * (2.0 * PI * u1).cos()
        })
        .collect();
    axes[0].bar(&[1.0, 2.0, 3.0, 4.0], &[3.0, 7.0, 2.0, 5.0])?;
    axes[0].set_title("bar")?;
    axes[1].hist(&normal, 30)?;
    axes[1].set_title("hist")?;
    let shifted: Vec<f64> = normal.iter().map(|v| v * 0.5 + 1.0).collect();
    axes[2].boxplot(&[normal.clone(), shifted.clone()])?;
    axes[2].set_title("boxplot")?;
    axes[3].violin(&[normal.clone(), shifted])?;
    axes[3].set_title("violin")?;
    let xs = linspace(0.0, 10.0, 50);
    let mid: Vec<f64> = xs.iter().map(|v| (v * 0.6).sin()).collect();
    let lo: Vec<f64> = mid.iter().map(|v| v - 0.3).collect();
    let hi: Vec<f64> = mid.iter().map(|v| v + 0.3).collect();
    axes[4].fill_between(&xs, &lo, &hi)?;
    axes[4].line(&xs, &mid)?;
    axes[4].set_title("fill_between")?;
    let xe = linspace(0.0, 10.0, 12);
    let ye: Vec<f64> = xe.iter().map(|v| v.sqrt()).collect();
    axes[5].errorbar(&xe, &ye, &vec![0.2; xe.len()])?;
    axes[5].stem(&xe, &ye)?;
    axes[5].set_title("errorbar + stem")?;
    fig.savefig(format!("{out}/stats.png"))?;

    // 3D surface and helix.
    let fig = session.figure_sized("3D", 1400, 700)?;
    let a = fig.subplot3d(1, 2, 1)?;
    let gx = linspace(-3.0, 3.0, 60);
    let gy = linspace(-3.0, 3.0, 60);
    let mut z = Vec::with_capacity(gx.len() * gy.len());
    for y in &gy {
        for x in &gx {
            z.push((x * x + y * y).sqrt().sin() / ((x * x + y * y).sqrt() + 0.3));
        }
    }
    a.surface(&gx, &gy, &z)?.set_colormap("viridis")?;
    a.set_title("surface")?;
    let b = fig.subplot3d(1, 2, 2)?;
    let t = linspace(0.0, 6.0 * PI, 600);
    let hx: Vec<f64> = t.iter().map(|v| v.cos()).collect();
    let hy: Vec<f64> = t.iter().map(|v| v.sin()).collect();
    b.line3d(&hx, &hy, &t)?.set_color("tab:orange")?;
    b.set_title("line3d")?;
    fig.savefig(format!("{out}/surface.png"))?;

    // Categories, log scale, styles.
    let fig = session.figure_sized("Styles", 1200, 600)?;
    fig.apply_style(PlotStyle::Paper)?;
    let [c, d] = <[Axes; 2]>::try_from(fig.subplots(1, 2)?).unwrap();
    c.bar_categories(
        &["apples", "pears", "plums"],
        &[4.0, 9.0, 6.0],
        &BarOptions::default(),
    )?;
    c.set_title("categories")?;
    let lx = linspace(1.0, 1000.0, 200);
    let ly: Vec<f64> = lx.iter().map(|v| v * v).collect();
    d.plot(&lx, &ly, "g-")?;
    d.set_xscale(Scale::Log)?
        .set_yscale(Scale::Log)?
        .set_title("log-log")?;
    fig.savefig(format!("{out}/styles.png"))?;

    std::fs::write(format!("{out}/lines.svg"), session.figures()[0].to_svg()?)?;
    println!("wrote gallery to {out}/");
    Ok(())
}
