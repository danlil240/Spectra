#include <cmath>
#include <spectra/easy.hpp>

int main()
{
    // ── 3D Shapes ──
    auto& sh = spectra::shapes3d();
    sh.label("3D Primitives");

    // Box
    sh.box(0.0f, 0.0f, 0.0f, 0.5f, 0.5f, 0.5f);

    // Sphere
    sh.sphere(2.0f, 0.0f, 0.0f, 0.6f).segments(48);

    // Cylinder
    sh.cylinder(4.0f, -0.5f, 0.0f, 4.0f, 0.5f, 0.0f, 0.3f);

    // Cone
    sh.cone(0.0f, 0.0f, 2.0f, 0.0f, 1.0f, 2.0f, 0.4f);

    // 3D Arrow
    sh.arrow3d(2.0f, 0.0f, 2.0f, 4.0f, 0.0f, 2.0f, 0.05f);

    // Plane
    sh.plane(2.0f, -1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.5f);

    // ── Animated 3D shapes ──
    auto& anim = spectra::shapes3d();
    anim.label("Animated").color(spectra::rgb(1.0f, 0.3f, 0.1f));

    spectra::on_update(
        [&](float /*dt*/, float t)
        {
            anim.clear_shapes();

            // Orbiting sphere
            float ox = 2.0f + 1.5f * std::cos(t);
            float oz = 1.0f + 1.5f * std::sin(t);
            anim.sphere(ox, 1.0f, oz, 0.25f);

            // Rotating arrow
            float ax = 2.0f + 1.0f * std::cos(t * 0.5f);
            float az = 1.0f + 1.0f * std::sin(t * 0.5f);
            anim.arrow3d(2.0f, 1.0f, 1.0f, ax, 1.5f, az, 0.03f);
        });

    spectra::show();

    return 0;
}
