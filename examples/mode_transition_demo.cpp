// Mode Transition Demo — demonstrates animated 2D↔3D view switching.
//
// Shows a figure with a sine wave in 2D, then transitions to a 3D helix view,
// and back. The transition animates camera, axis limits, grid planes, and
// 3D element opacity over 0.8 seconds.

#include <cmath>
#include <spectra/easy.hpp>
#include <vector>

int main()
{
    // Generate helix data
    const int N = 500;
    std::vector<float> x(N), y(N), z(N);
    for (int i = 0; i < N; ++i)
    {
        float t = static_cast<float>(i) / (N - 1) * 4.0f * 3.14159f;
        x[i] = std::cos(t);
        y[i] = std::sin(t);
        z[i] = t / (4.0f * 3.14159f) * 2.0f - 1.0f;
    }

    // Create figure with a 3D plot
    spectra::figure();
    spectra::plot3(x, y, z).color({0.122f, 0.467f, 0.706f}).width(2.5f).label("Helix");
    spectra::xlabel("X");
    spectra::ylabel("Y");
    spectra::zlabel("Z");
    spectra::gca3d()->auto_fit();

    // Set camera to a nice viewing angle
    spectra::gca3d()->camera().azimuth = 45.0f;
    spectra::gca3d()->camera().elevation = 25.0f;
    spectra::gca3d()->camera().update_position_from_orbit();

    spectra::show();
    return 0;
}
