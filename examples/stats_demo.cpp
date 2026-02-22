#include <spectra/easy.hpp>

#include <cmath>
#include <random>
#include <vector>

int main()
{
    // Generate sample data
    std::mt19937                    rng(42);
    std::normal_distribution<float> normal(0.0f, 1.0f);
    std::normal_distribution<float> normal2(2.0f, 0.5f);
    std::normal_distribution<float> normal3(-1.0f, 1.5f);

    std::vector<float> data1(200), data2(200), data3(200);
    for (auto& v : data1)
        v = normal(rng);
    for (auto& v : data2)
        v = normal2(rng);
    for (auto& v : data3)
        v = normal3(rng);

    // ── Box Plot ──
    spectra::subplot(2, 2, 1);
    auto& bp = spectra::box_plot();
    bp.add_box(1.0f, data1).add_box(2.0f, data2).add_box(3.0f, data3);
    bp.label("Distributions");
    spectra::title("Box Plot");
    spectra::grid(true);

    // ── Violin Plot ──
    spectra::subplot(2, 2, 2);
    auto& vn = spectra::violin();
    vn.add_violin(1.0f, data1).add_violin(2.0f, data2).add_violin(3.0f, data3);
    vn.label("KDE").color(spectra::colors::orange);
    spectra::title("Violin Plot");
    spectra::grid(true);

    // ── Histogram ──
    spectra::subplot(2, 2, 3);
    spectra::histogram(data1, 30).label("Normal(0,1)").color(spectra::colors::blue);
    spectra::title("Histogram");
    spectra::grid(true);

    // ── Bar Chart ──
    spectra::subplot(2, 2, 4);
    std::vector<float> categories = {1, 2, 3, 4, 5};
    std::vector<float> values     = {23, 45, 12, 67, 34};
    spectra::bar(categories, values).label("Sales").color(spectra::colors::green);
    spectra::title("Bar Chart");
    spectra::grid(true);

    spectra::show();
    return 0;
}
