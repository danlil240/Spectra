// Eigen integration demo — pass Eigen vectors directly to Spectra.
// Build with: cmake -DSPECTRA_USE_EIGEN=ON ..

#include <eigen3/Eigen/Core>
#include <cmath>
#include <spectra/eigen_easy.hpp>

int main()
{
    // Generate data using Eigen
    const int       N     = 200;
    Eigen::VectorXf x     = Eigen::VectorXf::LinSpaced(N, 0.0f, 4.0f * M_PI);
    Eigen::VectorXf y_sin = x.array().sin();
    Eigen::VectorXf y_cos = x.array().cos();
    Eigen::VectorXf y_exp = (-0.1f * x.array()).exp() * x.array().sin();

    // ── 2D: Direct Eigen vectors, no conversion needed ──
    spectra::subplot(2, 2, 1);
    spectra::plot(x, y_sin, "b-").label("sin(x)");
    spectra::plot(x, y_cos, "r--").label("cos(x)");
    spectra::title("Trigonometric Functions");
    spectra::legend();

    spectra::subplot(2, 2, 2);
    spectra::plot(x, y_exp, "g-").label("e^(-0.1x) sin(x)");
    spectra::title("Damped Oscillation");
    spectra::legend();

    // ── Scatter with Eigen ──
    Eigen::VectorXf sx = Eigen::VectorXf::Random(100) * 5.0f;
    Eigen::VectorXf sy = sx.array().square() + Eigen::VectorXf::Random(100).array() * 2.0f;

    spectra::subplot(2, 2, 3);
    spectra::scatter(sx, sy);
    spectra::title("Scatter (Eigen::Random)");
    spectra::xlabel("x");
    spectra::ylabel("y");

    // ── 3D: Helix with Eigen ──
    const int       M  = 500;
    Eigen::VectorXf t  = Eigen::VectorXf::LinSpaced(M, 0.0f, 6.0f * M_PI);
    Eigen::VectorXf hx = t.array().cos();
    Eigen::VectorXf hy = t.array().sin();
    Eigen::VectorXf hz = t / (6.0f * M_PI);

    spectra::subplot3d(2, 2, 4);
    spectra::plot3(hx, hy, hz).label("helix").color({0.2f, 0.6f, 1.0f, 1.0f});
    spectra::gca3d()->auto_fit();
    spectra::title("3D Helix");

    spectra::show();
    return 0;
}
