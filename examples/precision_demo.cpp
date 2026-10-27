// Precision showcase: the "scales & ticks" figure of the visual gallery.
//
//   log-log            ordinary float data on log10 axes
//   1e7 + small        y = 1e7 + 3 sin(x), built with the double-precision
//                      line(span<const double>, span<const double>) overload
//   tiny values        3e-6 sin(x), float is fine here
//   Unix timestamps    x = 1.7e9 + seconds, streamed point by point with the
//                      double-precision append(double, double)
//
// Float holds only ~7 significant digits: storing 1e7 + 3 sin(x) or an epoch
// timestamp as float quantises it into visible stairs.  The double overloads
// keep a double origin in x_offset()/y_offset() and store small float offsets.
//
// Usage: precision_demo [output.png]   (headless, writes the PNG and exits)

#include <cmath>
#include <iostream>
#include <random>
#include <spectra/spectra.hpp>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    const std::string out = argc > 1 ? argv[1] : "precision_demo.png";

    spectra::App app({.headless = true, .socket_path = "", .theme = "light"});
    auto&        fig = app.figure({.width = 1280, .height = 800});

    // ── log-log ──
    {
        auto&              ax = fig.subplot(2, 2, 1);
        constexpr size_t   N  = 200;
        std::vector<float> x(N);
        std::vector<float> y(N);
        for (size_t i = 0; i < N; ++i)
        {
            x[i] = std::pow(10.0f, -3.0f + 6.0f * static_cast<float>(i) / (N - 1));
            y[i] = x[i] * x[i];
        }
        ax.line(x, y).label("x^2");
        ax.xscale(spectra::ScaleType::Log10);
        ax.yscale(spectra::ScaleType::Log10);
        ax.title("log-log");
    }

    // ── 1e7 + small (double-precision span overload) ──
    {
        auto&               ax = fig.subplot(2, 2, 2);
        constexpr size_t    N  = 500;
        std::vector<double> x(N);
        std::vector<double> y(N);
        for (size_t i = 0; i < N; ++i)
        {
            x[i] = 10.0 * static_cast<double>(i) / (N - 1);
            y[i] = 1.0e7 + 3.0 * std::sin(x[i]);
        }
        ax.line(x, y).label("large offset");
        ax.title("Large offset (1e7 + small)");
    }

    // ── tiny values ──
    {
        auto&              ax = fig.subplot(2, 2, 3);
        constexpr size_t   N  = 500;
        std::vector<float> x(N);
        std::vector<float> y(N);
        for (size_t i = 0; i < N; ++i)
        {
            x[i] = 10.0f * static_cast<float>(i) / (N - 1);
            y[i] = 3.0e-6f * std::sin(x[i]);
        }
        ax.line(x, y).label("micro");
        ax.title("Tiny values (1e-6)");
    }

    // ── Unix timestamps (streamed with append(double, double)) ──
    {
        auto&                            ax = fig.subplot(2, 2, 4);
        auto&                            s  = ax.line().label("unix time");
        std::mt19937                     rng(42);
        std::normal_distribution<double> noise(0.0, 1.0);
        const double                     t0 = 1.7e9;
        for (int i = 0; i <= 400; ++i)
        {
            const double t = t0 + 216.0 * i;   // one day in 400 steps
            s.append(t, noise(rng));
        }
        ax.title("Unix timestamps on X");
    }

    fig.save_png(out);
    app.run();
    std::cout << "Saved " << out << "\n";
    return 0;
}
