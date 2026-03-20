#include <cmath>
#include <spectra/easy.hpp>
#include <vector>

int main()
{
    // ── Per-shape easy API: each call is independent ──

    // Red rectangle
    spectra::rect(1.0f, 1.0f, 2.0f, 1.5f)
        .fill_color({1.0f, 0.2f, 0.2f, 1.0f})
        .fill_opacity(0.4f);

    // Blue circle
    spectra::circle(6.0f, 5.0f, 1.2f)
        .fill_color({0.2f, 0.4f, 1.0f, 1.0f})
        .fill_opacity(0.5f);

    // Green ellipse
    spectra::ellipse(3.0f, 5.0f, 1.5f, 0.8f)
        .fill_color({0.2f, 0.8f, 0.3f, 1.0f})
        .fill_opacity(0.35f);

    // Orange arrow
    spectra::arrow(0.5f, 4.0f, 3.0f, 6.5f)
        .line_color({1.0f, 0.6f, 0.1f, 1.0f});

    // Purple ring
    spectra::ring(8.0f, 3.0f, 1.5f, 0.8f)
        .fill_color({0.6f, 0.2f, 0.8f, 1.0f})
        .fill_opacity(0.4f);

    // Cyan polygon (pentagon)
    constexpr int      N = 5;
    std::vector<float> px(N);
    std::vector<float> py(N);
    for (int i = 0; i < N; ++i)
    {
        float angle = 2.0f * 3.14159f * static_cast<float>(i) / N - 3.14159f / 2.0f;
        px[i]       = 8.0f + 1.0f * std::cos(angle);
        py[i]       = 6.0f + 1.0f * std::sin(angle);
    }
    spectra::polygon(px, py)
        .fill_color({0.1f, 0.8f, 0.8f, 1.0f})
        .fill_opacity(0.35f);

    // Rotated rectangle
    spectra::rect(5.0f, 1.0f, 2.5f, 1.0f)
        .rotation(0.4f)
        .fill_opacity(0.2f);

    // Line
    spectra::shape_line(0.0f, 0.0f, 10.0f, 8.0f);

    // Text labels at data-space coordinates
    spectra::text_annotation(1.5f, 0.5f, "Rectangle").text_align(1);
    spectra::text_annotation(6.0f, 5.0f, "Circle").text_align(1);
    spectra::text_annotation(3.0f, 5.0f, "Ellipse").text_align(1);
    spectra::text_annotation(8.0f, 6.0f, "Pentagon").text_align(1);

    // ── You can still use shapes() for grouped/animated shapes ──
    auto& anim_sh = spectra::shapes();
    anim_sh.label("Animated").color(spectra::rgb(1.0f, 0.4f, 0.2f));

    spectra::on_update(
        [&](float /*dt*/, float t)
        {
            anim_sh.clear_shapes();

            // Orbiting circle
            float cx = 5.0f + 3.0f * std::cos(t);
            float cy = 4.0f + 2.0f * std::sin(t);
            anim_sh.circle(cx, cy, 0.4f).fill_opacity(0.6f);

            // Pulsing ring
            float r = 0.8f + 0.3f * std::sin(t * 2.0f);
            anim_sh.ring(5.0f, 4.0f, r, r * 0.6f).fill_opacity(0.4f);

            // Rotating arrow
            float ax = 5.0f + 2.0f * std::cos(t * 0.7f);
            float ay = 4.0f + 2.0f * std::sin(t * 0.7f);
            anim_sh.arrow(5.0f, 4.0f, ax, ay);
        });

    spectra::title("Shapes Demo — Static + Animated");
    spectra::xlabel("X");
    spectra::ylabel("Y");
    spectra::xlim(-1.0f, 11.0f);
    spectra::ylim(-1.0f, 9.0f);
    spectra::grid(true);
    spectra::legend();

    spectra::show();

    return 0;
}
