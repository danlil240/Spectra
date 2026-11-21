//! End-to-end tests against a real `spectra-backend`.
//!
//! They run only when `SPECTRA_BACKEND_PATH` points at a backend binary (and
//! need a display, e.g. `xvfb-run -a cargo test`). Each test gets its own
//! session on one private broker socket.

use std::path::PathBuf;
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::{Arc, Mutex, OnceLock};
use std::time::Duration;

use spectra_plot::*;

fn socket() -> Option<&'static str> {
    static SOCKET: OnceLock<Option<String>> = OnceLock::new();
    SOCKET
        .get_or_init(|| {
            std::env::var_os("SPECTRA_BACKEND_PATH")?;
            let dir = std::env::temp_dir();
            Some(
                dir.join(format!("spectra-rust-it-{}.sock", std::process::id()))
                    .display()
                    .to_string(),
            )
        })
        .as_deref()
}

/// One test at a time talks to the broker, so launching it never races.
fn session() -> Option<(Session, std::sync::MutexGuard<'static, ()>)> {
    static SERIAL: Mutex<()> = Mutex::new(());
    let Some(sock) = socket() else {
        eprintln!("SPECTRA_BACKEND_PATH not set: skipping backend test");
        return None;
    };
    let guard = SERIAL.lock().unwrap_or_else(|e| e.into_inner());
    let s = Session::with_options(SessionOptions {
        socket: Some(sock.to_string()),
        auto_launch: true,
        client_name: "rust-it".into(),
    })
    .expect("connect to spectra-backend");
    Some((s, guard))
}

fn out_dir() -> PathBuf {
    let d = std::env::temp_dir().join(format!("spectra-rust-it-out-{}", std::process::id()));
    std::fs::create_dir_all(&d).unwrap();
    d
}

fn assert_png(path: &std::path::Path) {
    let bytes = std::fs::read(path).unwrap_or_else(|e| panic!("{}: {e}", path.display()));
    assert!(
        bytes.len() > 1000,
        "{} is only {} bytes",
        path.display(),
        bytes.len()
    );
    assert_eq!(&bytes[..8], b"\x89PNG\r\n\x1a\n");
}

#[test]
fn figures_are_listed_and_closed() {
    let Some((s, _g)) = session() else { return };
    let a = s.figure("a").unwrap();
    let b = s.figure_sized("b", 640, 480).unwrap();
    let ids = s.list_figures().unwrap();
    assert!(ids.contains(&a.id()) && ids.contains(&b.id()), "{ids:?}");
    assert_eq!(b.size(), (640, 480));
    a.close().unwrap();
    assert!(!s.list_figures().unwrap().contains(&a.id()));
    assert_eq!(s.figures().len(), 1);
}

#[test]
fn every_2d_series_type_renders() {
    let Some((s, _g)) = session() else { return };
    let fig = s.figure_sized("2d", 900, 600).unwrap();
    let axes = fig.subplots(3, 4).unwrap();
    let x = linspace(0.0, 6.0, 64);
    let y: Vec<f64> = x.iter().map(|v| v.sin()).collect();
    let data: Vec<f64> = (0..500).map(|i| ((i * 37) % 101) as f64 / 10.0).collect();

    let created = vec![
        axes[0].line(&x, &y).unwrap(),
        axes[1].scatter(&x, &y).unwrap(),
        axes[2].plot(&x, &y, "g--s").unwrap(),
        axes[3].step(&x, &y, DrawStyle::StepsMid).unwrap(),
        axes[3]
            .stairs(&[1.0, 3.0, 2.0], &[0.0, 1.0, 2.0, 3.0])
            .unwrap(),
        axes[4].bar(&[1.0, 2.0, 3.0], &[2.0, 5.0, 3.0]).unwrap(),
        axes[4].barh(&[1.0, 2.0], &[1.0, 2.0]).unwrap(),
        axes[5].hist(&data, 20).unwrap(),
        axes[6].fill_between(&x, &y, &vec![0.0; x.len()]).unwrap(),
        axes[7].stem(&x, &y).unwrap(),
        axes[8]
            .boxplot(&[data.clone(), data.iter().map(|v| v * 2.0).collect()])
            .unwrap(),
        axes[9].violin(std::slice::from_ref(&data)).unwrap(),
        axes[10].fplot(|v| v * v, -2.0, 2.0, 100).unwrap(),
        axes[10].hline(1.0).unwrap(),
        axes[10].vline(0.0).unwrap(),
    ];
    let eb = axes[11]
        .errorbar_with(
            &x,
            &y,
            &ErrorBarOptions {
                yerr: Some(ErrorSpec::Scalar(0.1)),
                xerr: Some(ErrorSpec::Symmetric(vec![0.05; x.len()])),
                capsize: 0.1,
                ..Default::default()
            },
        )
        .unwrap();
    assert!(eb.data.is_some() && eb.bars.is_some());
    let kinds: Vec<SeriesKind> = created.iter().map(|s| s.kind()).collect();
    assert!(
        kinds.contains(&SeriesKind::Box) && kinds.contains(&SeriesKind::Violin),
        "{kinds:?}"
    );

    let path = out_dir().join("all2d.png");
    fig.savefig(&path).unwrap();
    assert_png(&path);
}

#[test]
fn series_styling_round_trips() {
    let Some((s, _g)) = session() else { return };
    let fig = s.figure("style").unwrap();
    let ax = fig.subplot(1, 1, 1).unwrap();
    let line = ax.line(&[0.0, 1.0, 2.0], &[1.0, 0.0, 1.0]).unwrap();
    line.set_label("v")
        .unwrap()
        .set_color("#ff8800")
        .unwrap()
        .set_color("tab:blue")
        .unwrap()
        .set_color((0.1, 0.2, 0.3))
        .unwrap()
        .set_line_width(3.0)
        .unwrap()
        .set_marker_size(6.0)
        .unwrap()
        .set_line_style(LineStyle::DashDot)
        .unwrap()
        .set_marker(Marker::Diamond)
        .unwrap()
        .set_format("k:")
        .unwrap()
        .set_opacity(0.5)
        .unwrap()
        .set_draw_style(DrawStyle::StepsPost)
        .unwrap()
        .set_dense_mode(DenseMode::Exact)
        .unwrap()
        .set_visible(true)
        .unwrap();
    assert_eq!(line.label(), "v");
    assert!(matches!(
        line.set_color("not-a-colour"),
        Err(Error::InvalidArgument(_))
    ));

    ax.set_xlim(-1.0, 3.0)
        .unwrap()
        .set_ylim(-1.0, 2.0)
        .unwrap()
        .set_xlabel("x")
        .unwrap()
        .set_ylabel("y")
        .unwrap()
        .set_title("t")
        .unwrap()
        .grid(false)
        .unwrap()
        .legend(true)
        .unwrap()
        .set_yscale(Scale::Log)
        .unwrap()
        .set_xaxis_datetime(false)
        .unwrap();
    ax.batch(|b| {
        b.xlim(0.0, 2.0).ylim(0.0, 2.0).title("batched").grid(true);
    })
    .unwrap();
    fig.suptitle("super").unwrap();
    assert_eq!(fig.get_suptitle(), "super");
    for style in PlotStyle::ALL {
        fig.apply_style(style).unwrap();
    }
    fig.set_layout(Some(true), Some(Share::All), Some(Share::None))
        .unwrap();
    fig.tight_layout().unwrap();
    let path = out_dir().join("style.png");
    fig.savefig(&path).unwrap();
    assert_png(&path);
}

#[test]
fn removing_a_series_reindexes_the_rest() {
    let Some((s, _g)) = session() else { return };
    let fig = s.figure("remove").unwrap();
    let a = fig.subplot(2, 1, 1).unwrap();
    let b = fig.subplot(2, 1, 2).unwrap();
    let s0 = a.line(&[0.0, 1.0], &[0.0, 1.0]).unwrap();
    let s1 = a.line(&[0.0, 1.0], &[1.0, 0.0]).unwrap();
    let s2 = b.scatter(&[0.0, 1.0], &[0.5, 0.5]).unwrap();
    assert_eq!((s0.index(), s1.index(), s2.index()), (0, 1, 2));
    s0.remove().unwrap();
    assert_eq!((s1.index(), s2.index()), (0, 1));
    assert!(s0.set_label("gone").is_err());
    // The survivors still address the right series.
    s1.set_label("s1").unwrap();
    s2.set_data(&[0.0, 2.0], &[1.0, 1.0]).unwrap();
    assert_eq!(a.series().len(), 1);
    assert_eq!(b.series().len(), 1);
    a.clear().unwrap();
    assert!(a.series().is_empty());
    assert_eq!(s2.index(), 0);
}

#[test]
fn categorical_bars_and_bar_modes() {
    let Some((s, _g)) = session() else { return };
    let fig = s.figure("bars").unwrap();
    let ax = fig.subplot(1, 1, 1).unwrap();
    let opts = BarOptions {
        label: "2025".into(),
        ..Default::default()
    };
    ax.bar_categories(&["a", "b", "c"], &[1.0, 2.0, 3.0], &opts)
        .unwrap();
    ax.bar_categories(&["a", "b", "c"], &[2.0, 1.0, 4.0], &BarOptions::default())
        .unwrap();
    for mode in [BarMode::Grouped, BarMode::Stacked, BarMode::Overlay] {
        ax.set_bar_mode(mode).unwrap();
        assert_eq!(ax.bar_mode(), mode);
    }
    let path = out_dir().join("bars.png");
    fig.savefig(&path).unwrap();
    assert_png(&path);
}

#[test]
fn three_d_series_render() {
    let Some((s, _g)) = session() else { return };
    let fig = s.figure("3d").unwrap();
    let ax = fig.subplot3d(1, 2, 1).unwrap();
    assert!(ax.is_3d());
    let g = linspace(-1.0, 1.0, 20);
    let z: Vec<f64> = g
        .iter()
        .flat_map(|y| g.iter().map(move |x| x * x - y * y))
        .collect();
    let surf = ax.surface(&g, &g, &z).unwrap();
    surf.set_colormap("plasma")
        .unwrap()
        .set_colormap_range(Some((-1.0, 1.0)))
        .unwrap();
    surf.set_show_edges(true, None)
        .unwrap()
        .set_contour_projection(true, 8)
        .unwrap();
    ax.set_light_mode(LightMode::Fixed)
        .unwrap()
        .set_light_angles(45.0, 30.0)
        .unwrap();
    ax.set_fog(0.2).unwrap().set_zlim(-1.0, 1.0).unwrap();
    let b = fig.subplot3d(1, 2, 2).unwrap();
    let t = linspace(0.0, 10.0, 100);
    b.line3d(&t, &t, &t).unwrap();
    b.scatter3d(&t, &t, &t)
        .unwrap()
        .set_depth_shade(true)
        .unwrap()
        .set_color_values(&t)
        .unwrap()
        .set_colormap("turbo")
        .unwrap();
    b.colorbar(true).unwrap();
    // 2D-only calls are rejected on 3D axes and vice versa.
    assert!(b.bar(&[1.0], &[1.0]).is_err());
    assert!(fig.subplot(2, 2, 4).unwrap().surface(&g, &g, &z).is_err());
    let path = out_dir().join("3d.png");
    fig.savefig(&path).unwrap();
    assert_png(&path);
}

#[test]
fn annotations_update_and_clear() {
    let Some((s, _g)) = session() else { return };
    let fig = s.figure("text").unwrap();
    let ax = fig.subplot(1, 1, 1).unwrap();
    ax.line(&[0.0, 10.0], &[0.0, 10.0]).unwrap();
    let t = ax.text(1.0, 1.0, "hello").unwrap();
    let a = ax
        .annotate_with(
            "here",
            (5.0, 5.0),
            (2.0, 8.0),
            &TextOptions {
                halo: Some(Halo::default()),
                bbox: Some(TextBox::default()),
                ..TextOptions::default().color("red").unwrap()
            },
        )
        .unwrap();
    assert_eq!((t.index(), a.index()), (0, 1));
    t.set_text("moved").unwrap().set_position(3.0, 3.0).unwrap();
    t.set_font_size(18.0).unwrap().set_rotation(45.0).unwrap();
    t.set_alignment(HAlign::Center, VAlign::Middle).unwrap();
    a.set_arrow(None).unwrap().set_visible(false).unwrap();
    assert_eq!(ax.annotations().len(), 2);
    ax.clear_annotations().unwrap();
    assert!(ax.annotations().is_empty());
    assert!(t.set_text("x").is_err());
}

#[test]
fn streaming_append_and_large_data() {
    let Some((s, _g)) = session() else { return };
    let fig = s.figure("stream").unwrap();
    let ax = fig.subplot(1, 1, 1).unwrap();
    let line = ax.line(&[0.0f64; 0], &[0.0f64; 0]).unwrap();
    // Unix-time x values need the stream origin to stay precise in f32.
    let t0 = 1.7e9;
    for i in 0..50 {
        let xs: Vec<f64> = (0..100).map(|j| t0 + (i * 100 + j) as f64 * 0.01).collect();
        let ys: Vec<f64> = xs.iter().map(|v| (v - t0).sin()).collect();
        line.append(&xs, &ys).unwrap();
    }
    ax.set_xaxis_datetime(true).unwrap();
    let n = 2_000_000;
    let big_x = linspace(0.0, 1.0, n);
    let big_y: Vec<f32> = (0..n).map(|i| (i as f32 * 1e-4).sin()).collect();
    ax.line(&big_x, &big_y).unwrap();
    let png = fig.to_png(800, 400).unwrap();
    assert_eq!(&png[..4], b"\x89PNG");
}

#[test]
fn export_png_and_svg() {
    let Some((s, _g)) = session() else { return };
    let fig = s.figure("export").unwrap();
    fig.subplot(1, 1, 1)
        .unwrap()
        .plot_y(&[1.0, 3.0, 2.0], "r-o")
        .unwrap();
    let svg = fig.to_svg().unwrap();
    assert!(svg.contains("<svg"), "{}", &svg[..svg.len().min(200)]);
    let path = out_dir().join("light.png");
    fig.savefig_with(
        &path,
        &ExportOptions {
            width: 400,
            height: 300,
            theme: "light".into(),
            ..Default::default()
        },
    )
    .unwrap();
    assert_png(&path);
}

#[test]
fn knobs_read_back() {
    let Some((s, _g)) = session() else { return };
    let fig = s.figure("knobs").unwrap();
    fig.add_knob("gain", 0.25, 0.0, 1.0).unwrap();
    assert!(fig.add_knob("bad", 0.0, 1.0, 0.0).is_err());
    let v = s.knob("gain").unwrap();
    assert_eq!(v, Some(0.25));
}

#[test]
fn backend_animation_ticks() {
    let Some((s, _g)) = session() else { return };
    let fig = s.figure("anim").unwrap();
    let line = fig
        .subplot(1, 1, 1)
        .unwrap()
        .line(&[0.0, 1.0], &[0.0, 1.0])
        .unwrap();
    let frames = Arc::new(AtomicU32::new(0));
    let f2 = frames.clone();
    let anim = fig
        .animate(60.0, 0.0, move |frame| {
            f2.fetch_add(1, Ordering::SeqCst);
            let _ = line.set_data(&[0.0, 1.0], &[0.0, frame.t as f64]);
        })
        .unwrap();
    fig.show().unwrap();
    s.sleep(Duration::from_millis(800)).unwrap();
    anim.stop().unwrap();
    assert!(!anim.is_running());
    let n = frames.load(Ordering::SeqCst);
    assert!((10..=60).contains(&n), "{n} frames in 0.8 s at 60 fps");
    s.sleep(Duration::from_millis(100)).unwrap();
    assert_eq!(frames.load(Ordering::SeqCst), n, "frames after stop()");

    // A finite animation stops by itself.
    let last_t = Arc::new(Mutex::new(0.0f32));
    let lt = last_t.clone();
    let short = fig
        .animate(30.0, 0.3, move |f| *lt.lock().unwrap() = f.t)
        .unwrap();
    s.sleep(Duration::from_millis(600)).unwrap();
    assert!(!short.is_running());
    let t = *last_t.lock().unwrap();
    assert!((0.3..0.6).contains(&t), "last frame at t={t}");
    short.stop().unwrap();
    fig.close_window().unwrap();
}

#[test]
fn topics_publish_and_subscribe() {
    let Some((s, _g)) = session() else { return };
    let sock = socket().unwrap().to_string();
    let mut publisher = Publisher::with_options(
        "rust/it/accel",
        PublisherOptions {
            unit: "m/s^2".into(),
            socket: Some(sock.clone()),
            ..Default::default()
        },
    )
    .unwrap();
    let mut p3 = Publisher::with_options(
        "rust/it/pos",
        PublisherOptions {
            kind: TopicKind::Scalar3D,
            socket: Some(sock),
            ..Default::default()
        },
    )
    .unwrap();
    let topics = s.list_topics().unwrap();
    let names: Vec<&str> = topics.iter().map(|t| t.name.as_str()).collect();
    assert!(
        names.contains(&"rust/it/accel") && names.contains(&"rust/it/pos"),
        "{names:?}"
    );

    let fig = s.figure("topics").unwrap();
    let ax = fig.subplot(1, 1, 1).unwrap();
    let live = ax.subscribe_topic("rust/it/accel").unwrap();
    assert_eq!(live.topic().as_deref(), Some("rust/it/accel"));
    for i in 0..100 {
        publisher
            .publish(i as f64 * 0.01, (i as f64 * 0.1).sin())
            .unwrap();
    }
    publisher.publish_batch(&[1.0, 1.1], &[0.0, 0.5]).unwrap();
    p3.publish3(1.0, 2.0, 3.0).unwrap();
    assert!(publisher.publish3(1.0, 2.0, 3.0).is_err());
    assert!(publisher.publish_interleaved(&[1.0, 2.0, 3.0]).is_err());
    live.unsubscribe_topic().unwrap();
    assert_eq!(live.topic(), None);
    publisher.close();
    assert!(!publisher.is_connected());
}

#[test]
fn events_report_closed_windows() {
    let Some((s, _g)) = session() else { return };
    let fig = s.figure("events").unwrap();
    fig.subplot(1, 1, 1)
        .unwrap()
        .line(&[0.0, 1.0], &[0.0, 1.0])
        .unwrap();
    fig.show().unwrap();
    assert!(fig.is_visible());
    s.poll_events().unwrap();
    fig.close_window().unwrap();
    assert!(!fig.is_visible());
}

#[test]
fn invalid_input_is_rejected_locally() {
    let Some((s, _g)) = session() else { return };
    assert!(s.figure_sized("zero", 0, 10).is_err());
    let fig = s.figure("bad").unwrap();
    let ax = fig.subplot(1, 1, 1).unwrap();
    assert!(matches!(
        ax.line(&[0.0, 1.0], &[0.0]),
        Err(Error::InvalidArgument(_))
    ));
    assert!(ax.plot(&[0.0], &[0.0], "zz").is_err());
    assert!(fig.subplot(1, 1, 2).is_err());
}

#[test]
fn session_outlives_agent_heartbeat_timeout() {
    // The backend drops render agents that send no heartbeat for 15 s (checked
    // every 5 s); a
    // client session must not be mistaken for one.
    let Some((s, _g)) = session() else { return };
    let fig = s.figure("idle").unwrap();
    fig.subplot(1, 1, 1)
        .unwrap()
        .line(&[0.0, 1.0], &[0.0, 1.0])
        .unwrap();
    s.sleep(Duration::from_secs(22)).unwrap();
    assert!(s.is_connected());
    assert!(s.list_figures().unwrap().contains(&fig.id()));
}
