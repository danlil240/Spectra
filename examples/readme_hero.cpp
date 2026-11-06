// readme_hero — renders the animated clips used at the top of README.md.
//
//   readme_hero stream  out.mp4   live multi-signal scope (2D)
//   readme_hero surface out.mp4   orbiting lit 3D surface
//
// Headless, so it also runs in CI or over SSH.  Convert to a GIF with
// tools/make_readme_gif.sh.  Requires SPECTRA_USE_FFMPEG=ON.

#include <cmath>
#include <iostream>
#include <spectra/spectra.hpp>
#include <string>
#include <vector>

#ifdef SPECTRA_USE_FFMPEG
namespace
{

constexpr float kDuration = 6.0f;
constexpr float kFps      = 30.0f;

int record_stream(const std::string& out)
{
    spectra::App app({.headless = true, .socket_path = "", .theme = "night"});
    auto&        fig = app.figure({.width = 1280, .height = 720});

    fig.legend().position = spectra::LegendPosition::TopRight;   // no jumping

    constexpr size_t   N      = 4000;
    constexpr float    window = 8.0f;
    std::vector<float> x(N), a(N), b(N), c(N);

    auto& top = fig.subplot(2, 1, 1);
    auto& bot = fig.subplot(2, 1, 2);
    top.title("Live sensor stream");
    top.ylabel("Signal");
    bot.xlabel("Time (s)");
    bot.ylabel("Error");
    top.ylim(-2.2f, 2.2f);
    bot.ylim(-1.2f, 1.2f);

    auto& la = top.line(x, a).label("accel x");
    auto& lb = top.line(x, b).label("accel y");
    auto& lc = bot.line(x, c).label("tracking error").color(spectra::rgb(0.65f, 0.89f, 0.63f));

    auto fill = [&](float t)
    {
        for (size_t i = 0; i < N; ++i)
        {
            float s = t - window + window * static_cast<float>(i) / (N - 1);
            x[i]    = s;
            a[i]    = std::sin(2.1f * s) + 0.35f * std::sin(13.0f * s) + 0.1f * std::sin(57.0f * s);
            b[i]    = 0.8f * std::cos(1.3f * s) + 0.25f * std::sin(23.0f * s + 1.0f);
            c[i]    = 0.6f * std::sin(0.7f * s) * std::exp(-0.05f * std::fmod(s, 20.0f))
                   + 0.15f * std::sin(31.0f * s);
        }
        la.set_x(x).set_y(a);
        lb.set_x(x).set_y(b);
        lc.set_x(x).set_y(c);
        top.xlim(t - window, t);
        bot.xlim(t - window, t);
    };
    fill(window);

    fig.animate()
        .fps(kFps)
        .duration(kDuration)
        .on_frame([&](spectra::Frame& f) { fill(window + f.elapsed_seconds()); })
        .record(out);

    app.run();
    return 0;
}

int record_surface(const std::string& out)
{
    spectra::App app({.headless = true, .socket_path = "", .theme = "night"});
    auto&        fig = app.figure({.width = 1280, .height = 720});
    auto&        ax  = fig.subplot3d(1, 1, 1);
    ax.title("Lit 3D surface");
    ax.light_dir(0.5f, 0.8f, 1.0f);
    ax.lighting_enabled(true);

    constexpr int      N = 90;
    std::vector<float> xs(N), ys(N), zs(N * N);
    for (int i = 0; i < N; ++i)
    {
        xs[i] = ys[i] = -4.0f + 8.0f * static_cast<float>(i) / (N - 1);
    }
    for (int j = 0; j < N; ++j)
    {
        for (int i = 0; i < N; ++i)
        {
            float r       = std::sqrt(xs[i] * xs[i] + ys[j] * ys[j]);
            zs[j * N + i] = 2.0f * std::sin(r) / (0.3f + 0.25f * r);
        }
    }
    ax.surface(xs, ys, zs)
        .ambient(0.18f)
        .specular(0.5f)
        .shininess(48.0f)
        .colormap(spectra::ColormapType::Viridis);
    ax.xlim(-4.0f, 4.0f);
    ax.ylim(-4.0f, 4.0f);
    ax.camera().set_azimuth(30.0f).set_elevation(30.0f);

    fig.animate()
        .fps(kFps)
        .duration(kDuration)
        .on_frame([&](spectra::Frame& f)
                  { ax.camera().set_azimuth(30.0f + 60.0f * f.elapsed_seconds()); })
        .record(out);

    app.run();
    return 0;
}

}   // namespace
#endif

int main([[maybe_unused]] int argc, [[maybe_unused]] char** argv)
{
#ifndef SPECTRA_USE_FFMPEG
    std::cerr << "This example requires SPECTRA_USE_FFMPEG=ON\n";
    return 1;
#else
    const std::string scene = argc > 1 ? argv[1] : "stream";
    const std::string out   = argc > 2 ? argv[2] : scene + ".mp4";
    if (scene == "stream")
        return record_stream(out);
    if (scene == "surface")
        return record_surface(out);
    std::cerr << "usage: readme_hero [stream|surface] [out.mp4]\n";
    return 2;
#endif
}
