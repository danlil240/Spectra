#include <spectra/easy.hpp>

#include <cmath>
#include <random>
#include <vector>

int main()
{
    // Generate rich sample data
    std::mt19937 rng(42);

    // Three distinct distributions for box/violin plots
    std::normal_distribution<float> dist_a(50.0f, 12.0f);   // centered, moderate spread
    std::normal_distribution<float> dist_b(65.0f, 8.0f);    // higher, tighter
    std::normal_distribution<float> dist_c(40.0f, 18.0f);   // lower, wider
    std::normal_distribution<float> dist_d(72.0f, 6.0f);    // high, tight
    std::normal_distribution<float> dist_e(55.0f, 15.0f);   // moderate

    auto gen = [&](auto& dist, int n)
    {
        std::vector<float> v(n);
        for (auto& x : v)
            x = dist(rng);
        return v;
    };

    auto a = gen(dist_a, 300);
    auto b = gen(dist_b, 300);
    auto c = gen(dist_c, 300);
    auto d = gen(dist_d, 300);
    auto e = gen(dist_e, 300);

    // Modern color palette (vibrant but harmonious)
    const spectra::Color teal   = {0.15f, 0.78f, 0.75f, 1.0f};
    const spectra::Color coral  = {1.00f, 0.42f, 0.42f, 1.0f};
    const spectra::Color violet = {0.55f, 0.36f, 0.96f, 1.0f};
    const spectra::Color amber  = {1.00f, 0.72f, 0.22f, 1.0f};
    const spectra::Color sky    = {0.30f, 0.60f, 1.00f, 1.0f};

    // ── Box Plot ──
    spectra::subplot(2, 2, 1);
    auto& bp = spectra::box_plot();
    bp.add_box(1.0f, a).add_box(2.0f, b).add_box(3.0f, c).add_box(4.0f, d).add_box(5.0f, e);
    bp.label("Scores").color(teal).box_width(0.5f).gradient(true);
    spectra::title("Box Plot — Score Distribution");
    spectra::xlabel("Group");
    spectra::ylabel("Score");
    spectra::grid(true);

    // ── Violin Plot ──
    spectra::subplot(2, 2, 2);
    auto& vn = spectra::violin();
    vn.add_violin(1.0f, a).add_violin(2.0f, b).add_violin(3.0f, c).add_violin(4.0f, d).add_violin(
        5.0f,
        e);
    vn.label("Density").color(violet).violin_width(0.7f).resolution(60);
    spectra::title("Violin Plot — Density Estimate");
    spectra::xlabel("Group");
    spectra::ylabel("Score");
    spectra::grid(true);

    // ── Histogram ──
    spectra::subplot(2, 2, 3);
    // Combine all data for a rich histogram
    std::vector<float> combined;
    combined.insert(combined.end(), a.begin(), a.end());
    combined.insert(combined.end(), c.begin(), c.end());
    spectra::histogram(combined, 35).label("Combined A+C").color(sky).gradient(false);
    spectra::title("Histogram — Frequency");
    spectra::xlabel("Value");
    spectra::ylabel("Count");
    spectra::grid(true);

    // ── Bar Chart ──
    spectra::subplot(2, 2, 4);
    std::vector<float> months  = {1, 2, 3, 4, 5, 6, 7, 8};
    std::vector<float> revenue = {42, 58, 35, 72, 65, 88, 76, 94};
    spectra::bar(months, revenue).label("Revenue ($K)").color(coral).bar_width(0.6f);
    spectra::title("Bar Chart — Monthly Revenue");
    spectra::xlabel("Month");
    spectra::ylabel("Revenue ($K)");
    spectra::grid(true);

    spectra::show();
    return 0;
}
