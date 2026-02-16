#include <plotix/app.hpp>
#include <plotix/axes3d.hpp>
#include <plotix/figure.hpp>
#include <plotix/series3d.hpp>

#include <cmath>
#include <vector>

int main() {
    using namespace plotix;

    AppConfig config;
    App app(config);

    auto& fig = app.figure();

    // ── Subplot 1: Overlapping transparent surfaces ──────────────────────
    {
        auto& ax = fig.subplot3d(2, 2, 1);
        ax.xlabel("X");
        ax.ylabel("Y");
        ax.zlabel("Z");
        ax.light_dir(0.5f, 0.8f, 1.0f);

        const int N = 60;
        std::vector<float> x(N), y(N);
        for (int i = 0; i < N; ++i) {
            x[i] = -3.0f + 6.0f * static_cast<float>(i) / (N - 1);
            y[i] = -3.0f + 6.0f * static_cast<float>(i) / (N - 1);
        }

        // Surface 1: sin wave (opaque)
        std::vector<float> z1(N * N);
        for (int j = 0; j < N; ++j)
            for (int i = 0; i < N; ++i)
                z1[j * N + i] = std::sin(x[i]) * std::cos(y[j]) * 2.0f;

        auto& s1 = ax.surface(x, y, z1);
        s1.color(Color{0.2f, 0.5f, 1.0f, 1.0f})
          .ambient(0.15f)
          .specular(0.4f)
          .shininess(48.0f)
          .colormap(ColormapType::Viridis);

        // Surface 2: cos wave (semi-transparent, shifted up)
        std::vector<float> z2(N * N);
        for (int j = 0; j < N; ++j)
            for (int i = 0; i < N; ++i)
                z2[j * N + i] = std::cos(x[i] * 0.8f) * std::sin(y[j] * 0.8f) * 1.5f + 2.0f;

        auto& s2 = ax.surface(x, y, z2);
        s2.color(Color{1.0f, 0.3f, 0.2f, 0.5f})  // 50% transparent
          .ambient(0.2f)
          .specular(0.5f)
          .shininess(32.0f);

        ax.xlim(-3.0f, 3.0f);
        ax.ylim(-3.0f, 3.0f);
        ax.zlim(-3.0f, 5.0f);
    }

    // ── Subplot 2: Transparent scatter with opaque lines ─────────────────
    {
        auto& ax = fig.subplot3d(2, 2, 2);
        ax.xlabel("X");
        ax.ylabel("Y");
        ax.zlabel("Z");

        // Opaque helix line
        const int N = 200;
        std::vector<float> lx(N), ly(N), lz(N);
        for (int i = 0; i < N; ++i) {
            float t = static_cast<float>(i) / (N - 1) * 4.0f * 3.14159f;
            lx[i] = std::cos(t) * 2.0f;
            ly[i] = std::sin(t) * 2.0f;
            lz[i] = t * 0.3f - 2.0f;
        }
        ax.line3d(lx, ly, lz)
          .color(Color{0.1f, 0.8f, 0.3f, 1.0f})
          .width(2.5f);

        // Semi-transparent scatter cloud around the helix
        const int M = 500;
        std::vector<float> sx(M), sy(M), sz(M);
        for (int i = 0; i < M; ++i) {
            float t = static_cast<float>(i) / (M - 1) * 4.0f * 3.14159f;
            float noise_x = (static_cast<float>(i * 7 % 100) / 100.0f - 0.5f) * 1.5f;
            float noise_y = (static_cast<float>(i * 13 % 100) / 100.0f - 0.5f) * 1.5f;
            float noise_z = (static_cast<float>(i * 17 % 100) / 100.0f - 0.5f) * 1.0f;
            sx[i] = std::cos(t) * 2.0f + noise_x;
            sy[i] = std::sin(t) * 2.0f + noise_y;
            sz[i] = t * 0.3f - 2.0f + noise_z;
        }
        ax.scatter3d(sx, sy, sz)
          .color(Color{0.9f, 0.4f, 0.1f, 0.4f})  // 60% transparent
          .size(8.0f);

        ax.xlim(-4.0f, 4.0f);
        ax.ylim(-4.0f, 4.0f);
        ax.zlim(-3.0f, 5.0f);
    }

    // ── Subplot 3: Wireframe surface ─────────────────────────────────────
    {
        auto& ax = fig.subplot3d(2, 2, 3);
        ax.xlabel("X");
        ax.ylabel("Y");
        ax.zlabel("Z");

        const int N = 40;
        std::vector<float> x(N), y(N);
        for (int i = 0; i < N; ++i) {
            x[i] = -3.0f + 6.0f * static_cast<float>(i) / (N - 1);
            y[i] = -3.0f + 6.0f * static_cast<float>(i) / (N - 1);
        }

        std::vector<float> z(N * N);
        for (int j = 0; j < N; ++j)
            for (int i = 0; i < N; ++i) {
                float r = std::sqrt(x[i] * x[i] + y[j] * y[j]);
                z[j * N + i] = std::sin(r) * 2.0f / (r + 0.5f);
            }

        auto& s = ax.surface(x, y, z);
        s.color(Color{0.3f, 0.7f, 1.0f, 0.8f})
         .wireframe(true);

        ax.xlim(-3.0f, 3.0f);
        ax.ylim(-3.0f, 3.0f);
        ax.zlim(-2.0f, 3.0f);
    }

    // ── Subplot 4: Transparent mesh with opacity control ─────────────────
    {
        auto& ax = fig.subplot3d(2, 2, 4);
        ax.xlabel("X");
        ax.ylabel("Y");
        ax.zlabel("Z");
        ax.light_dir(1.0f, 0.5f, 0.8f);

        // Create a simple icosahedron-like mesh
        // 12 vertices, 20 triangles
        std::vector<float> verts = {
            // pos (x,y,z), normal (nx,ny,nz)
             0.0f,  2.0f,  0.0f,   0.0f,  1.0f,  0.0f,  // top
             1.8f,  0.8f,  0.0f,   0.9f,  0.4f,  0.0f,
             0.6f,  0.8f,  1.7f,   0.3f,  0.4f,  0.9f,
            -1.5f,  0.8f,  1.0f,  -0.7f,  0.4f,  0.5f,
            -1.5f,  0.8f, -1.0f,  -0.7f,  0.4f, -0.5f,
             0.6f,  0.8f, -1.7f,   0.3f,  0.4f, -0.9f,
             1.5f, -0.8f,  1.0f,   0.7f, -0.4f,  0.5f,
            -0.6f, -0.8f,  1.7f,  -0.3f, -0.4f,  0.9f,
            -1.8f, -0.8f,  0.0f,  -0.9f, -0.4f,  0.0f,
            -0.6f, -0.8f, -1.7f,  -0.3f, -0.4f, -0.9f,
             1.5f, -0.8f, -1.0f,   0.7f, -0.4f, -0.5f,
             0.0f, -2.0f,  0.0f,   0.0f, -1.0f,  0.0f,  // bottom
        };

        std::vector<uint32_t> indices = {
            0,1,2,  0,2,3,  0,3,4,  0,4,5,  0,5,1,
            1,6,2,  2,7,3,  3,8,4,  4,9,5,  5,10,1,
            6,7,2,  7,8,3,  8,9,4,  9,10,5, 10,6,1,
            11,7,6, 11,8,7, 11,9,8, 11,10,9, 11,6,10,
        };

        auto& m = ax.mesh(verts, indices);
        m.color(Color{0.8f, 0.2f, 0.9f, 0.6f})  // 40% transparent
         .opacity(0.7f)
         .ambient(0.2f)
         .specular(0.6f)
         .shininess(64.0f);

        ax.xlim(-3.0f, 3.0f);
        ax.ylim(-3.0f, 3.0f);
        ax.zlim(-3.0f, 3.0f);
    }

    app.run();
    return 0;
}
