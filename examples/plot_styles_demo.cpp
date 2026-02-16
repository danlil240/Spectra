#include <cmath>
#include <spectra/spectra.hpp>
#include <vector>

int main()
{
    spectra::App app;
    auto& fig = app.figure({.width = 1920, .height = 1080});

    // ── 2x2 subplot grid showcasing all MATLAB-style plot customization ──

    constexpr size_t N = 200;
    std::vector<float> x(N);
    for (size_t i = 0; i < N; ++i)
        x[i] = static_cast<float>(i) * 0.05f;

    // ─────────────────────────────────────────────────────────────────────
    // Subplot 1: Line styles — solid, dashed, dotted, dash-dot, dash-dot-dot
    // ─────────────────────────────────────────────────────────────────────
    {
        auto& ax = fig.subplot(2, 2, 1);

        std::vector<float> y1(N), y2(N), y3(N), y4(N), y5(N);
        for (size_t i = 0; i < N; ++i)
        {
            y1[i] = std::sin(x[i]);
            y2[i] = std::sin(x[i]) + 1.2f;
            y3[i] = std::sin(x[i]) + 2.4f;
            y4[i] = std::sin(x[i]) - 1.2f;
            y5[i] = std::sin(x[i]) - 2.4f;
        }

        // MATLAB-style format strings: "color line_style"
        ax.plot(x, y3, "r-");                          // red solid
        ax.plot(x, y2, "b--");                         // blue dashed
        ax.plot(x, y1, "g:");                          // green dotted
        ax.plot(x, y4, "m-.");                         // magenta dash-dot
        ax.plot(x, y5, "c-..").label("Dash-Dot-Dot");  // cyan dash-dot-dot

        // Label the top ones via runtime API
        ax.series()[0]->label("Solid (-)");
        ax.series()[1]->label("Dashed (--)");
        ax.series()[2]->label("Dotted (:)");
        ax.series()[3]->label("Dash-Dot (-.)");

        ax.title("Line Styles");
        ax.xlabel("X");
        ax.ylabel("Amplitude");
        ax.xlim(0.0f, 10.0f);
        ax.ylim(-4.0f, 4.0f);
    }

    // ─────────────────────────────────────────────────────────────────────
    // Subplot 2: Marker shapes — all 18 marker types on scatter plots
    // ─────────────────────────────────────────────────────────────────────
    {
        auto& ax = fig.subplot(2, 2, 2);

        // Place markers on a grid to show each shape clearly
        const spectra::MarkerStyle markers[] = {
            spectra::MarkerStyle::Point,
            spectra::MarkerStyle::Circle,
            spectra::MarkerStyle::Plus,
            spectra::MarkerStyle::Cross,
            spectra::MarkerStyle::Star,
            spectra::MarkerStyle::Square,
            spectra::MarkerStyle::Diamond,
            spectra::MarkerStyle::TriangleUp,
            spectra::MarkerStyle::TriangleDown,
            spectra::MarkerStyle::TriangleLeft,
            spectra::MarkerStyle::TriangleRight,
            spectra::MarkerStyle::Pentagon,
            spectra::MarkerStyle::Hexagon,
            spectra::MarkerStyle::FilledCircle,
            spectra::MarkerStyle::FilledSquare,
            spectra::MarkerStyle::FilledDiamond,
            spectra::MarkerStyle::FilledTriangleUp,
        };
        constexpr int num_markers = 17;

        // Use the default color palette for variety
        const spectra::Color palette[] = {
            spectra::colors::red,
            spectra::colors::blue,
            spectra::colors::green,
            spectra::colors::cyan,
            spectra::colors::magenta,
            spectra::colors::yellow,
            spectra::rgb(1.0f, 0.5f, 0.0f),  // orange
            spectra::rgb(0.5f, 0.0f, 1.0f),  // purple
        };

        for (int i = 0; i < num_markers; ++i)
        {
            int row = i / 6;
            int col = i % 6;
            float px = static_cast<float>(col) * 1.5f + 1.0f;
            float py = static_cast<float>(2 - row) * 2.0f + 1.0f;

            std::vector<float> sx = {px};
            std::vector<float> sy = {py};

            auto& s = ax.scatter(sx, sy).color(palette[i % 8]).size(14.0f);
            s.marker_style(markers[i]);
            s.label(spectra::marker_style_name(markers[i]));
        }

        ax.title("Marker Shapes (17 types)");
        ax.xlabel("");
        ax.ylabel("");
        ax.xlim(-0.5f, 10.0f);
        ax.ylim(-0.5f, 7.0f);
    }

    // ─────────────────────────────────────────────────────────────────────
    // Subplot 3: Combined line + marker — MATLAB "r--o", "b:*", etc.
    // ─────────────────────────────────────────────────────────────────────
    {
        auto& ax = fig.subplot(2, 2, 3);

        constexpr size_t M = 30;
        std::vector<float> xm(M);
        std::vector<float> y1(M), y2(M), y3(M), y4(M);
        for (size_t i = 0; i < M; ++i)
        {
            xm[i] = static_cast<float>(i) * 0.3f;
            y1[i] = std::sin(xm[i]);
            y2[i] = std::cos(xm[i]);
            y3[i] = std::sin(xm[i] * 0.5f) * 1.5f;
            y4[i] = std::cos(xm[i] * 0.7f) * 0.8f;
        }

        // Combined format strings: color + line style + marker
        ax.plot(xm, y1, "r--o").label("r--o  (red dashed + circle)");
        ax.plot(xm, y2, "b:*").label("b:*   (blue dotted + star)");
        ax.plot(xm, y3, "g-.s").label("g-.s  (green dash-dot + square)");
        ax.plot(xm, y4, "m-d").label("m-d   (magenta solid + diamond)");

        ax.title("Line + Marker Combos");
        ax.xlabel("X");
        ax.ylabel("Y");
        ax.xlim(0.0f, 9.0f);
        ax.ylim(-2.0f, 2.0f);
    }

    // ─────────────────────────────────────────────────────────────────────
    // Subplot 4: Runtime style mutation + opacity + PlotStyle struct API
    // ─────────────────────────────────────────────────────────────────────
    {
        auto& ax = fig.subplot(2, 2, 4);

        constexpr size_t P = 150;
        std::vector<float> xp(P);
        std::vector<float> y1(P), y2(P), y3(P);
        for (size_t i = 0; i < P; ++i)
        {
            xp[i] = static_cast<float>(i) * 0.04f;
            y1[i] = std::sin(xp[i] * 2.0f);
            y2[i] = std::cos(xp[i] * 1.5f) * 0.8f;
            y3[i] = std::sin(xp[i] * 3.0f) * 0.5f;
        }

        // Create series first, then mutate styles via runtime API
        auto& s1 = ax.line(xp, y1).label("Runtime styled").color(spectra::colors::red);
        auto& s2 = ax.line(xp, y2).label("PlotStyle struct").color(spectra::colors::blue);
        auto& s3 = ax.line(xp, y3).label("Opacity 0.4").color(spectra::colors::green);

        // Runtime mutation: change line style and add markers after creation
        s1.line_style(spectra::LineStyle::Dashed);
        s1.marker_style(spectra::MarkerStyle::FilledCircle);
        s1.marker_size(8.0f);
        s1.width(2.5f);

        // PlotStyle struct: set everything at once
        spectra::PlotStyle ps;
        ps.line_style = spectra::LineStyle::DashDot;
        ps.marker_style = spectra::MarkerStyle::Star;
        ps.marker_size = 10.0f;
        ps.opacity = 0.85f;
        s2.plot_style(ps);

        // Opacity demonstration
        s3.line_style(spectra::LineStyle::Solid);
        s3.opacity(0.4f);
        s3.width(4.0f);

        ax.title("Runtime Mutation & Opacity");
        ax.xlabel("X");
        ax.ylabel("Y");
        ax.xlim(0.0f, 6.0f);
        ax.ylim(-1.5f, 1.5f);
    }

    fig.show();
    app.run();

    return 0;
}
