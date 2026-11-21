// Grid-valued series: imshow, a labelled heatmap, contour lines and filled
// contours, with colorbars.  Pass an output path to render headless instead:
//   grid_plots_demo out.png
#include <cmath>
#include <spectra/spectra.hpp>
#include <vector>

int main(int argc, char** argv)
{
    const bool   headless = argc > 1;
    spectra::App app({.headless = headless, .socket_path = ""});
    auto&        fig = app.figure({.width = 1400, .height = 900});

    // A smooth 40x30 field: two lobes on a gentle slope.
    constexpr size_t   C = 40;
    constexpr size_t   R = 30;
    std::vector<float> z(C * R);
    for (size_t r = 0; r < R; ++r)
        for (size_t c = 0; c < C; ++c)
        {
            const float x = (static_cast<float>(c) - 20.0f) / 8.0f;
            const float y = (static_cast<float>(r) - 15.0f) / 8.0f;
            z[r * C + c]  = std::sin(x * 1.3f) * std::cos(y * 1.1f) + 0.3f * x;
        }

    auto& img = fig.subplot(2, 2, 1);
    img.imshow(z, C, R);
    img.title("imshow");
    img.show_colorbar(true);

    // Heatmap: each cell's value is printed in it; names label the axes.
    std::vector<float> sales = {12, 15, 9, 21, 18, 22, 25, 19, 7, 11, 14, 16};
    auto&              heat  = fig.subplot(2, 2, 2);
    heat.heatmap(sales, 4, 3, {"Q1", "Q2", "Q3", "Q4"}, {"north", "south", "east"});
    heat.title("heatmap");
    heat.show_colorbar(true);

    auto& lines = fig.subplot(2, 2, 3);
    lines.contour(z, C, R, 10);
    lines.title("contour");
    lines.show_colorbar(true);

    auto& filled = fig.subplot(2, 2, 4);
    filled.contourf(z, C, R, 8);
    filled.title("contourf");
    filled.show_colorbar(true);

    if (headless)
        fig.save_png(argv[1]);
    app.run();
    return 0;
}
