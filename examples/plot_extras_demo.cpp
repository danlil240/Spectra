// Step lines, dense-line rendering and annotation boxes / halos / rotation.
//
//   plot_extras_demo [out.png] [--plain]
//
// Renders headless to out.png (default plot_extras.png).  --plain draws the
// same data the way older versions did (straight lines, every point, plain
// text) for comparison.

#include <cmath>
#include <iostream>
#include <random>
#include <spectra/spectra.hpp>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    std::string out   = "plot_extras.png";
    bool        plain = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--plain")
            plain = true;
        else
            out = a;
    }

    spectra::App app({.headless = true, .socket_path = "", .theme = "light"});
    auto&        fig = app.figure({.width = 1200, .height = 900});

    // ── Dense data: 300k noisy points per line ──
    auto& dense = fig.subplot(2, 1, 1);
    {
        constexpr size_t                N = 300000;
        std::mt19937                    rng(42);
        std::normal_distribution<float> noise(0.0f, 1.0f);
        std::vector<float>              x(N), y1(N), y2(N);
        for (size_t i = 0; i < N; ++i)
        {
            x[i]  = static_cast<float>(i) * 30.0f / N;
            y1[i] = std::sin(x[i]) + 0.45f * noise(rng);
            y2[i] = 2.0f + 0.5f * std::cos(x[i]) + 0.1f * noise(rng);
        }
        auto& a = dense.line(x, y1).label("noisy (300k pts, density)");
        auto& b = dense.line(x, y2).label("cleaner (300k pts, auto min/max)");
        a.dense_mode(plain ? spectra::DenseLineMode::Exact : spectra::DenseLineMode::Density);
        b.dense_mode(plain ? spectra::DenseLineMode::Exact : spectra::DenseLineMode::Auto);
        dense.title("Dense data");
    }

    // ── Steps and annotations ──
    auto& steps = fig.subplot(2, 1, 2);
    {
        const std::vector<float> t    = {0, 1, 2, 3, 4, 5, 6, 7, 8};
        const std::vector<float> mode = {0, 0, 1, 1, 3, 2, 2, 4, 4};
        auto&                    pre  = steps.plot(t, mode, "-o");
        pre.label("steps-pre");
        std::vector<float> m2(mode), m3(mode);
        for (auto& v : m2)
            v += 5.0f;
        for (auto& v : m3)
            v += 10.0f;
        auto& mid  = steps.plot(t, m2, "-s").label("steps-mid");
        auto& post = steps.plot(t, m3, "-^").label("steps-post");
        if (!plain)
        {
            pre.draw_style(spectra::DrawStyle::StepsPre);
            mid.draw_style(spectra::DrawStyle::StepsMid);
            post.draw_style(spectra::DrawStyle::StepsPost);
        }

        auto& box  = steps.annotate("mode change", 4.0f, 13.0f, 2.2f, 13.5f);
        auto& halo = steps.text(5.6f, 7.0f, "halo over the line");
        auto& rot  = steps.text(7.4f, 1.0f, "rotated 30\xC2\xB0");
        box.font_size(16.0f);
        halo.font_size(16.0f);
        rot.font_size(16.0f);
        if (!plain)
        {
            box.bbox(true);
            halo.halo(true).halo_width(2.5f);
            rot.rotation(30.0f).bbox(true);
        }
        steps.title("Step draw styles and annotations");
        steps.xlim(-0.5f, 8.5f);
        steps.ylim(-1.0f, 16.0f);
    }

    fig.save_png(out);
    app.run();
    std::cout << "Saved " << out << "\n";
    return 0;
}
