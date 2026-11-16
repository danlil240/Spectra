// Depth cues and surface styling in 3D: a depth-shaded helix (scatter and
// line), and a lit surface with edge lines over its faces, contour lines
// projected on the floor of the box and a headlight that follows the camera.
//
// Usage: depth_cues_3d [output.png [output.svg]]   (headless, writes and exits)

#include <cmath>
#include <spectra/spectra.hpp>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    const std::string png = argc > 1 ? argv[1] : "depth_cues_3d.png";
    const std::string svg = argc > 2 ? argv[2] : "";

    spectra::App app({.headless = true, .socket_path = "", .theme = "light"});
    auto&        fig = app.figure({.width = 1600, .height = 800});

    // ── Helix: points and line fade with depth ──
    auto& helix = fig.subplot3d(1, 2, 1);
    {
        constexpr int      N = 240;
        std::vector<float> x(N), y(N), z(N);
        for (int i = 0; i < N; ++i)
        {
            const float t             = static_cast<float>(i) * 0.08f;
            x[static_cast<size_t>(i)] = std::cos(t);
            y[static_cast<size_t>(i)] = std::sin(t);
            z[static_cast<size_t>(i)] = t * 0.15f;
        }
        helix.scatter3d(x, y, z).label("points").size(9.0f);   // depth_shade() is on by default
        helix.line3d(x, y, z).label("path").width(1.5f).depth_shade(true);
        helix.title("Depth-shaded helix");
    }

    // ── Surface: edges over faces, floor contours, headlight ──
    auto& surf_ax = fig.subplot3d(1, 2, 2);
    {
        constexpr int      NX = 40, NY = 40;
        std::vector<float> xg(NX), yg(NY), zv(static_cast<size_t>(NX * NY));
        for (int i = 0; i < NX; ++i)
            xg[static_cast<size_t>(i)] = -3.0f + 6.0f * static_cast<float>(i) / (NX - 1);
        for (int j = 0; j < NY; ++j)
            yg[static_cast<size_t>(j)] = -3.0f + 6.0f * static_cast<float>(j) / (NY - 1);
        for (int j = 0; j < NY; ++j)
            for (int i = 0; i < NX; ++i)
            {
                const float x = xg[static_cast<size_t>(i)], y = yg[static_cast<size_t>(j)];
                zv[static_cast<size_t>(j * NX + i)] =
                    std::exp(-(x * x + y * y) / 4.0f) * std::cos(x) * 2.0f;
            }
        surf_ax.surface(xg, yg, zv)
            .colormap("viridis")
            .show_edges(true)
            .contour_projection(true)
            .label("surface");
        surf_ax.light_mode(spectra::LightMode::Headlight);
        surf_ax.title("Edges, floor contours, headlight");
    }

    fig.save_png(png);
    if (!svg.empty())
        fig.save_svg(svg);
    app.run();
    return 0;
}
