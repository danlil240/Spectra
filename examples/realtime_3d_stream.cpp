// realtime_3d_stream.cpp — 3D real-time data stream visualization.
//
// Demonstrates:
//   - Three concurrent live 3D data streams (scatter3d updated every frame)
//   - Sliding trail buffer: each stream keeps the last N points (ring buffer)
//   - Animated orbit camera with slow auto-rotation
//   - Two tabbed views: 3D stream view + 2D signal monitor (XY projections)
//   - Interactive knobs: trail length, stream speeds, camera auto-rotate toggle
//
// Use-cases: IMU sensor fusion, multi-axis vibration monitoring,
//            flight trajectory streaming, molecular dynamics visualization.

#define _USE_MATH_DEFINES
#include <algorithm>
#include <cmath>
#include <spectra/easy.hpp>
#include <vector>

static constexpr int   MAX_TRAIL   = 512;   // max ring buffer capacity
static constexpr float TWO_PI      = 6.283185307f;
static constexpr float CAM_AZIMUTH = 45.0f;

// ─── Ring buffer helper ──────────────────────────────────────────────────────

struct Trail3D
{
    std::vector<float> x, y, z;
    int                head  = 0;
    int                count = 0;
    int                cap   = MAX_TRAIL;

    Trail3D()
    {
        x.resize(MAX_TRAIL, 0.0f);
        y.resize(MAX_TRAIL, 0.0f);
        z.resize(MAX_TRAIL, 0.0f);
    }

    void push(float px, float py, float pz)
    {
        x[head] = px;
        y[head] = py;
        z[head] = pz;
        head    = (head + 1) % cap;
        if (count < cap)
            ++count;
    }

    // Fill output vectors with the trail in chronological order, up to `cap` points.
    void get(std::vector<float>& ox, std::vector<float>& oy, std::vector<float>& oz) const
    {
        int n = std::min(count, cap);
        ox.resize(n);
        oy.resize(n);
        oz.resize(n);
        int start = (count < cap) ? 0 : head;
        for (int i = 0; i < n; ++i)
        {
            int idx = (start + i) % cap;
            ox[i]   = x[idx];
            oy[i]   = y[idx];
            oz[i]   = z[idx];
        }
    }
};

// ─── Stream generators ───────────────────────────────────────────────────────
// Each stream is a distinct Lissajous-like trajectory with noise injection.

struct Stream
{
    float   freq_x, freq_y, freq_z;   // oscillation frequencies
    float   phase_x, phase_y, phase_z;
    float   radius;
    Trail3D trail;

    Stream(float fx, float fy, float fz, float px, float py, float pz, float r)
        : freq_x(fx), freq_y(fy), freq_z(fz), phase_x(px), phase_y(py), phase_z(pz), radius(r)
    {
    }

    void sample(float t, int trail_cap)
    {
        trail.cap = std::clamp(trail_cap, 8, MAX_TRAIL);

        float px = radius * std::sin(freq_x * t + phase_x);
        float py = radius * std::cos(freq_y * t + phase_y);
        float pz = radius * std::sin(freq_z * t + phase_z) * std::cos(freq_x * 0.5f * t);

        trail.push(px, py, pz);
    }
};

// ─── Main ────────────────────────────────────────────────────────────────────

int main()
{
    // ── Knobs (interactive parameters) ──────────────────────────────────────
    auto& k_trail  = spectra::knob("Trail Length", 128.0f, 8.0f, 512.0f, 8.0f);
    auto& k_speed  = spectra::knob("Stream Speed", 1.0f, 0.1f, 5.0f);
    auto& k_rotate = spectra::knob_bool("Auto-Rotate Camera", true);

    // ── Tab 1: 3D stream view ────────────────────────────────────────────────
    auto& fig3d = spectra::figure(1280, 800);

    auto& ax3d = spectra::subplot3d(1, 1, 1);
    ax3d.title("3D Real-Time Data Streams");
    ax3d.xlabel("X");
    ax3d.ylabel("Y");
    ax3d.zlabel("Z");
    ax3d.grid(true);
    ax3d.grid_planes(spectra::Axes3D::GridPlane::All);
    ax3d.xlim(-1.5f, 1.5f);
    ax3d.ylim(-1.5f, 1.5f);
    ax3d.zlim(-1.5f, 1.5f);
    ax3d.camera().set_azimuth(CAM_AZIMUTH).set_elevation(25.0f);

    // Seed the 3D axes with initial series so the animation guard doesn't
    // kill the callback on the first frame (it checks for non-empty series).
    std::vector<float> seed = {0.0f};
    ax3d.scatter3d(seed, seed, seed).color(spectra::colors::cyan).size(3.0f).label("Stream A");
    ax3d.scatter3d(seed, seed, seed).color(spectra::colors::orange).size(3.0f).label("Stream B");
    ax3d.scatter3d(seed, seed, seed).color(spectra::colors::magenta).size(3.0f).label("Stream C");

    // ── Tab 2: 2D signal monitor (XY projections of all streams) ─────────────
    spectra::tab();

    auto& ax_xy = spectra::subplot(1, 1, 1);
    ax_xy.title("Signal Monitor — XY Projection");
    ax_xy.xlabel("X");
    ax_xy.ylabel("Y");
    ax_xy.xlim(-1.6f, 1.6f);
    ax_xy.ylim(-1.6f, 1.6f);
    ax_xy.grid(true);

    // 2D projection series (one per stream)
    auto& proj_a =
        ax_xy.plot(seed, seed).color(spectra::colors::cyan).width(1.2f).label("Stream A");
    auto& proj_b =
        ax_xy.plot(seed, seed).color(spectra::colors::orange).width(1.2f).label("Stream B");
    auto& proj_c =
        ax_xy.plot(seed, seed).color(spectra::colors::magenta).width(1.2f).label("Stream C");

    auto& fig2d            = *spectra::gcf();
    fig2d.legend().visible = true;

    // ── Data streams ─────────────────────────────────────────────────────────
    // Stream A: Lissajous 3:2 (cyan)
    Stream stream_a(3.0f, 2.0f, 1.5f, 0.0f, 0.5f, 1.0f, 1.2f);
    // Stream B: Lissajous 5:4 (orange) — shifted phase
    Stream stream_b(5.0f, 4.0f, 2.0f, 1.1f, 0.3f, 0.7f, 0.9f);
    // Stream C: Spiraling helix (magenta)
    Stream stream_c(1.0f, 1.0f, 3.5f, 0.0f, TWO_PI / 3.0f, TWO_PI * 2.0f / 3.0f, 1.1f);

    std::vector<float> ox, oy, oz;   // reuse per frame to avoid allocs

    // ── Real-time update loop ─────────────────────────────────────────────────
    // Only the active tab's animation callback fires, so we wire ALL update
    // logic (3D scatter + 2D projections) to fig3d.  When the user switches
    // to the 2D tab, fig2d becomes active — its callback drives the same data.
    auto update_all = [&](spectra::Frame& f)
    {
        float t        = f.elapsed_seconds();
        float speed    = k_speed.value;
        int   trail_n  = static_cast<int>(k_trail.value);
        bool  rotating = k_rotate.value > 0.5f;
        float st       = t * speed;

        // Sample new points into ring buffers
        stream_a.sample(st, trail_n);
        stream_b.sample(st, trail_n);
        stream_c.sample(st, trail_n);

        // ── Update 3D scatter series (clear+re-add each frame) ──────────
        ax3d.clear_series();

        stream_a.trail.get(ox, oy, oz);
        ax3d.scatter3d(ox, oy, oz).color(spectra::colors::cyan).size(3.0f).label("Stream A");

        stream_b.trail.get(ox, oy, oz);
        ax3d.scatter3d(ox, oy, oz).color(spectra::colors::orange).size(3.0f).label("Stream B");

        stream_c.trail.get(ox, oy, oz);
        ax3d.scatter3d(ox, oy, oz).color(spectra::colors::magenta).size(3.0f).label("Stream C");

        // ── 2D projection update ─────────────────────────────────────────
        stream_a.trail.get(ox, oy, oz);
        proj_a.set_x(ox);
        proj_a.set_y(oy);

        stream_b.trail.get(ox, oy, oz);
        proj_b.set_x(ox);
        proj_b.set_y(oy);

        stream_c.trail.get(ox, oy, oz);
        proj_c.set_x(ox);
        proj_c.set_y(oy);

        // ── Slow camera auto-rotation ────────────────────────────────────
        if (rotating)
        {
            ax3d.camera().azimuth = CAM_AZIMUTH + t * 18.0f;   // 18 deg/sec
            ax3d.camera().update_position_from_orbit();
        }
    };

    fig3d.animate().fps(60).on_frame(update_all).play();
    fig2d.animate().fps(60).on_frame(update_all).play();

    spectra::show();
    return 0;
}
