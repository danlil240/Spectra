#include <benchmark/benchmark.h>
#include <cmath>
#include <spectra/spectra.hpp>
#include <vector>

using namespace spectra;

// ═══════════════════════════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════════════════════════

struct SurfaceGrid
{
    std::vector<float> x, y, z;
    int                nx, ny;
};

static SurfaceGrid make_surface(int nx, int ny, float x0, float x1, float y0, float y1)
{
    SurfaceGrid g;
    g.nx = nx;
    g.ny = ny;
    g.x.resize(nx);
    g.y.resize(ny);
    g.z.resize(nx * ny);
    for (int i = 0; i < nx; ++i)
        g.x[i] = x0 + (x1 - x0) * static_cast<float>(i) / (nx - 1);
    for (int j = 0; j < ny; ++j)
        g.y[j] = y0 + (y1 - y0) * static_cast<float>(j) / (ny - 1);
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i)
            g.z[j * nx + i] = std::sin(g.x[i]) * std::cos(g.y[j]);
    return g;
}

struct MeshGrid
{
    std::vector<float>    vertices;
    std::vector<uint32_t> indices;
};

static MeshGrid make_mesh(int nx, int ny)
{
    MeshGrid m;
    m.vertices.reserve(static_cast<size_t>(nx) * ny * 6);
    m.indices.reserve(static_cast<size_t>(nx - 1) * (ny - 1) * 6);
    for (int j = 0; j < ny; ++j)
    {
        for (int i = 0; i < nx; ++i)
        {
            float x = static_cast<float>(i) / (nx - 1) * 4.0f - 2.0f;
            float y = static_cast<float>(j) / (ny - 1) * 4.0f - 2.0f;
            float z = std::sin(x) * std::cos(y);
            m.vertices.push_back(x);
            m.vertices.push_back(y);
            m.vertices.push_back(z);
            // Normal (approximate up)
            m.vertices.push_back(0.0f);
            m.vertices.push_back(0.0f);
            m.vertices.push_back(1.0f);
        }
    }
    for (int j = 0; j < ny - 1; ++j)
    {
        for (int i = 0; i < nx - 1; ++i)
        {
            uint32_t tl = static_cast<uint32_t>(j * nx + i);
            uint32_t tr = tl + 1;
            uint32_t bl = tl + static_cast<uint32_t>(nx);
            uint32_t br = bl + 1;
            m.indices.push_back(tl);
            m.indices.push_back(bl);
            m.indices.push_back(tr);
            m.indices.push_back(tr);
            m.indices.push_back(bl);
            m.indices.push_back(br);
        }
    }
    return m;
}

// ═══════════════════════════════════════════════════════════════════════════════
// 1. Lit Surface Rendering
// ═══════════════════════════════════════════════════════════════════════════════

static void BM_LitSurface_50x50(benchmark::State& state)
{
    App   app({.headless = true, .socket_path = ""});
    auto& fig = app.figure({.width = 800, .height = 600});
    auto& ax  = fig.subplot3d(1, 1, 1);

    auto sg = make_surface(50, 50, -3.0f, 3.0f, -3.0f, 3.0f);
    ax.surface(sg.x, sg.y, sg.z)
        .color(colors::orange)
        .ambient(0.2f)
        .specular(0.5f)
        .shininess(64.0f);
    ax.set_light_dir(1.0f, 1.0f, 1.0f);

    for (auto _ : state)
    {
        app.run();
        benchmark::DoNotOptimize(ax);
    }
    state.SetItemsProcessed(state.iterations() * 50 * 50);
}
BENCHMARK(BM_LitSurface_50x50);

static void BM_LitSurface_100x100(benchmark::State& state)
{
    App   app({.headless = true, .socket_path = ""});
    auto& fig = app.figure({.width = 800, .height = 600});
    auto& ax  = fig.subplot3d(1, 1, 1);

    auto sg = make_surface(100, 100, -5.0f, 5.0f, -5.0f, 5.0f);
    ax.surface(sg.x, sg.y, sg.z).color(colors::red).ambient(0.15f).specular(0.6f).shininess(128.0f);
    ax.set_light_dir(0.5f, 0.7f, 1.0f);

    for (auto _ : state)
    {
        app.run();
        benchmark::DoNotOptimize(ax);
    }
    state.SetItemsProcessed(state.iterations() * 100 * 100);
}
BENCHMARK(BM_LitSurface_100x100);

static void BM_LitSurface_500x500(benchmark::State& state)
{
    App   app({.headless = true, .socket_path = ""});
    auto& fig = app.figure({.width = 800, .height = 600});
    auto& ax  = fig.subplot3d(1, 1, 1);

    auto sg = make_surface(500, 500, -5.0f, 5.0f, -5.0f, 5.0f);
    ax.surface(sg.x, sg.y, sg.z)
        .color(colors::yellow)
        .ambient(0.2f)
        .specular(0.5f)
        .shininess(64.0f);

    for (auto _ : state)
    {
        app.run();
        benchmark::DoNotOptimize(ax);
    }
    state.SetItemsProcessed(state.iterations() * 500 * 500);
}
BENCHMARK(BM_LitSurface_500x500);

// ═══════════════════════════════════════════════════════════════════════════════
// 2. Lit Mesh Rendering
// ═══════════════════════════════════════════════════════════════════════════════

static void BM_LitMesh_1K(benchmark::State& state)
{
    App   app({.headless = true, .socket_path = ""});
    auto& fig = app.figure({.width = 800, .height = 600});
    auto& ax  = fig.subplot3d(1, 1, 1);

    auto mg = make_mesh(23, 23);   // ~968 triangles
    ax.mesh(mg.vertices, mg.indices)
        .color(colors::cyan)
        .ambient(0.2f)
        .specular(0.5f)
        .shininess(64.0f);

    for (auto _ : state)
    {
        app.run();
        benchmark::DoNotOptimize(ax);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(mg.indices.size() / 3));
}
BENCHMARK(BM_LitMesh_1K);

static void BM_LitMesh_100K(benchmark::State& state)
{
    App   app({.headless = true, .socket_path = ""});
    auto& fig = app.figure({.width = 800, .height = 600});
    auto& ax  = fig.subplot3d(1, 1, 1);

    auto mg = make_mesh(225, 225);   // ~100K triangles
    ax.mesh(mg.vertices, mg.indices)
        .color(colors::green)
        .ambient(0.15f)
        .specular(0.8f)
        .shininess(128.0f);

    for (auto _ : state)
    {
        app.run();
        benchmark::DoNotOptimize(ax);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(mg.indices.size() / 3));
}
BENCHMARK(BM_LitMesh_100K);

// ═══════════════════════════════════════════════════════════════════════════════
// 3. Transparent Series Rendering
// ═══════════════════════════════════════════════════════════════════════════════

static void BM_TransparentScatter3D_10K(benchmark::State& state)
{
    App   app({.headless = true, .socket_path = ""});
    auto& fig = app.figure({.width = 800, .height = 600});
    auto& ax  = fig.subplot3d(1, 1, 1);

    std::vector<float> x(10000), y(10000), z(10000);
    for (size_t i = 0; i < 10000; ++i)
    {
        float t = static_cast<float>(i) * 0.001f;
        x[i]    = std::cos(t) * t;
        y[i]    = std::sin(t) * t;
        z[i]    = t;
    }
    ax.scatter3d(x, y, z)
        .color(Color{0.0f, 0.5f, 1.0f, 0.5f})
        .size(3.0f)
        .blend_mode(BlendMode::Alpha);

    for (auto _ : state)
    {
        app.run();
        benchmark::DoNotOptimize(ax);
    }
    state.SetItemsProcessed(state.iterations() * 10000);
}
BENCHMARK(BM_TransparentScatter3D_10K);

static void BM_TransparentSurface_50x50(benchmark::State& state)
{
    App   app({.headless = true, .socket_path = ""});
    auto& fig = app.figure({.width = 800, .height = 600});
    auto& ax  = fig.subplot3d(1, 1, 1);

    auto sg = make_surface(50, 50, -3.0f, 3.0f, -3.0f, 3.0f);
    ax.surface(sg.x, sg.y, sg.z)
        .color(Color{1.0f, 0.5f, 0.0f, 0.6f})
        .ambient(0.2f)
        .specular(0.4f)
        .shininess(32.0f);

    for (auto _ : state)
    {
        app.run();
        benchmark::DoNotOptimize(ax);
    }
    state.SetItemsProcessed(state.iterations() * 50 * 50);
}
BENCHMARK(BM_TransparentSurface_50x50);

static void BM_TransparentSurface_100x100(benchmark::State& state)
{
    App   app({.headless = true, .socket_path = ""});
    auto& fig = app.figure({.width = 800, .height = 600});
    auto& ax  = fig.subplot3d(1, 1, 1);

    auto sg = make_surface(100, 100, -5.0f, 5.0f, -5.0f, 5.0f);
    ax.surface(sg.x, sg.y, sg.z)
        .color(Color{0.0f, 0.8f, 0.2f, 0.4f})
        .ambient(0.15f)
        .specular(0.5f)
        .shininess(64.0f);

    for (auto _ : state)
    {
        app.run();
        benchmark::DoNotOptimize(ax);
    }
    state.SetItemsProcessed(state.iterations() * 100 * 100);
}
BENCHMARK(BM_TransparentSurface_100x100);

static void BM_TransparentMesh_10K(benchmark::State& state)
{
    App   app({.headless = true, .socket_path = ""});
    auto& fig = app.figure({.width = 800, .height = 600});
    auto& ax  = fig.subplot3d(1, 1, 1);

    auto mg = make_mesh(75, 75);   // ~10K triangles
    ax.mesh(mg.vertices, mg.indices)
        .color(Color{0.5f, 0.5f, 0.5f, 0.5f})
        .ambient(0.2f)
        .specular(0.6f)
        .shininess(64.0f);

    for (auto _ : state)
    {
        app.run();
        benchmark::DoNotOptimize(ax);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(mg.indices.size() / 3));
}
BENCHMARK(BM_TransparentMesh_10K);

// ═══════════════════════════════════════════════════════════════════════════════
// 4. Mixed Opaque + Transparent (Painter's Sort)
// ═══════════════════════════════════════════════════════════════════════════════

static void BM_MixedOpaqueTransparent(benchmark::State& state)
{
    App   app({.headless = true, .socket_path = ""});
    auto& fig = app.figure({.width = 800, .height = 600});
    auto& ax  = fig.subplot3d(1, 1, 1);

    // Opaque surface
    auto sg1 = make_surface(30, 30, -3.0f, 3.0f, -3.0f, 3.0f);
    ax.surface(sg1.x, sg1.y, sg1.z)
        .color(colors::blue)
        .ambient(0.2f)
        .specular(0.5f)
        .shininess(64.0f);

    // Transparent scatter overlay
    std::vector<float> x(5000), y(5000), z(5000);
    for (size_t i = 0; i < 5000; ++i)
    {
        float t = static_cast<float>(i) * 0.002f;
        x[i]    = std::cos(t) * 2.0f;
        y[i]    = std::sin(t) * 2.0f;
        z[i]    = std::sin(t * 3.0f);
    }
    ax.scatter3d(x, y, z).color(Color{1.0f, 0.0f, 0.0f, 0.4f}).size(4.0f);

    for (auto _ : state)
    {
        app.run();
        benchmark::DoNotOptimize(ax);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MixedOpaqueTransparent);

static void BM_MultipleTransparentLayers(benchmark::State& state)
{
    App   app({.headless = true, .socket_path = ""});
    auto& fig = app.figure({.width = 800, .height = 600});
    auto& ax  = fig.subplot3d(1, 1, 1);

    // 3 overlapping transparent surfaces at different Z offsets
    for (int layer = 0; layer < 3; ++layer)
    {
        auto sg = make_surface(30, 30, -2.0f, 2.0f, -2.0f, 2.0f);
        // Offset Z values
        float offset = static_cast<float>(layer) * 0.5f;
        for (auto& z : sg.z)
            z += offset;

        float alpha = 0.3f + static_cast<float>(layer) * 0.15f;
        ax.surface(sg.x, sg.y, sg.z)
            .color(Color{static_cast<float>(layer) * 0.4f,
                         0.5f,
                         1.0f - static_cast<float>(layer) * 0.3f,
                         alpha})
            .ambient(0.2f)
            .specular(0.4f)
            .shininess(32.0f);
    }

    for (auto _ : state)
    {
        app.run();
        benchmark::DoNotOptimize(ax);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MultipleTransparentLayers);

// ═══════════════════════════════════════════════════════════════════════════════
// 5. Wireframe Rendering
// ═══════════════════════════════════════════════════════════════════════════════

static void BM_WireframeSurface_50x50(benchmark::State& state)
{
    App   app({.headless = true, .socket_path = ""});
    auto& fig = app.figure({.width = 800, .height = 600});
    auto& ax  = fig.subplot3d(1, 1, 1);

    auto sg = make_surface(50, 50, -3.0f, 3.0f, -3.0f, 3.0f);
    ax.surface(sg.x, sg.y, sg.z).color(colors::green).wireframe(true);

    for (auto _ : state)
    {
        app.run();
        benchmark::DoNotOptimize(ax);
    }
    state.SetItemsProcessed(state.iterations() * 50 * 50);
}
BENCHMARK(BM_WireframeSurface_50x50);

static void BM_WireframeSurface_100x100(benchmark::State& state)
{
    App   app({.headless = true, .socket_path = ""});
    auto& fig = app.figure({.width = 800, .height = 600});
    auto& ax  = fig.subplot3d(1, 1, 1);

    auto sg = make_surface(100, 100, -5.0f, 5.0f, -5.0f, 5.0f);
    ax.surface(sg.x, sg.y, sg.z).color(colors::cyan).wireframe(true);

    for (auto _ : state)
    {
        app.run();
        benchmark::DoNotOptimize(ax);
    }
    state.SetItemsProcessed(state.iterations() * 100 * 100);
}
BENCHMARK(BM_WireframeSurface_100x100);

// ═══════════════════════════════════════════════════════════════════════════════
// 6. Material Property Overhead
// ═══════════════════════════════════════════════════════════════════════════════

static void BM_MaterialPropertySet(benchmark::State& state)
{
    SurfaceSeries s;
    for (auto _ : state)
    {
        s.ambient(0.2f)
            .specular(0.5f)
            .shininess(64.0f)
            .color(Color{1.0f, 0.0f, 0.0f, 1.0f})
            .opacity(0.8f);
        benchmark::DoNotOptimize(s);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MaterialPropertySet);

static void BM_TransparencyCheck(benchmark::State& state)
{
    SurfaceSeries s;
    s.color(Color{1.0f, 0.0f, 0.0f, 0.5f}).opacity(0.8f);
    for (auto _ : state)
    {
        bool t = s.is_transparent();
        benchmark::DoNotOptimize(t);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_TransparencyCheck);

// ═══════════════════════════════════════════════════════════════════════════════
// 7. Centroid Computation (Painter's Sort)
// ═══════════════════════════════════════════════════════════════════════════════

static void BM_CentroidScatter3D_10K(benchmark::State& state)
{
    ScatterSeries3D    scatter;
    std::vector<float> x(10000), y(10000), z(10000);
    for (size_t i = 0; i < 10000; ++i)
    {
        float t = static_cast<float>(i) * 0.001f;
        x[i]    = std::cos(t) * t;
        y[i]    = std::sin(t) * t;
        z[i]    = t;
    }
    scatter.set_x(x).set_y(y).set_z(z);

    for (auto _ : state)
    {
        vec3 c = scatter.compute_centroid();
        benchmark::DoNotOptimize(c);
    }
    state.SetItemsProcessed(state.iterations() * 10000);
}
BENCHMARK(BM_CentroidScatter3D_10K);

static void BM_CentroidSurface_100x100(benchmark::State& state)
{
    auto          sg = make_surface(100, 100, -5.0f, 5.0f, -5.0f, 5.0f);
    SurfaceSeries s(sg.x, sg.y, sg.z);

    for (auto _ : state)
    {
        vec3 c = s.compute_centroid();
        benchmark::DoNotOptimize(c);
    }
    state.SetItemsProcessed(state.iterations() * 100 * 100);
}
BENCHMARK(BM_CentroidSurface_100x100);

static void BM_BoundsComputation_10K(benchmark::State& state)
{
    ScatterSeries3D    scatter;
    std::vector<float> x(10000), y(10000), z(10000);
    for (size_t i = 0; i < 10000; ++i)
    {
        float t = static_cast<float>(i) * 0.001f;
        x[i]    = std::cos(t) * t;
        y[i]    = std::sin(t) * t;
        z[i]    = t;
    }
    scatter.set_x(x).set_y(y).set_z(z);

    for (auto _ : state)
    {
        vec3 min_b, max_b;
        scatter.get_bounds(min_b, max_b);
        benchmark::DoNotOptimize(min_b);
        benchmark::DoNotOptimize(max_b);
    }
    state.SetItemsProcessed(state.iterations() * 10000);
}
BENCHMARK(BM_BoundsComputation_10K);

// ═══════════════════════════════════════════════════════════════════════════════
// 8. Wireframe Mesh Generation (CPU)
// ═══════════════════════════════════════════════════════════════════════════════

static void BM_WireframeMeshGen_50x50(benchmark::State& state)
{
    auto sg = make_surface(50, 50, -3.0f, 3.0f, -3.0f, 3.0f);
    for (auto _ : state)
    {
        SurfaceSeries s(sg.x, sg.y, sg.z);
        s.generate_wireframe_mesh();
        benchmark::DoNotOptimize(s.wireframe_mesh());
    }
    state.SetItemsProcessed(state.iterations() * 50 * 50);
}
BENCHMARK(BM_WireframeMeshGen_50x50);

static void BM_WireframeMeshGen_200x200(benchmark::State& state)
{
    auto sg = make_surface(200, 200, -5.0f, 5.0f, -5.0f, 5.0f);
    for (auto _ : state)
    {
        SurfaceSeries s(sg.x, sg.y, sg.z);
        s.generate_wireframe_mesh();
        benchmark::DoNotOptimize(s.wireframe_mesh());
    }
    state.SetItemsProcessed(state.iterations() * 200 * 200);
}
BENCHMARK(BM_WireframeMeshGen_200x200);

// ═══════════════════════════════════════════════════════════════════════════════
// 9. Colormap Sampling
// ═══════════════════════════════════════════════════════════════════════════════

static void BM_ColormapSample_Viridis(benchmark::State& state)
{
    for (auto _ : state)
    {
        for (int i = 0; i < 1000; ++i)
        {
            float t = static_cast<float>(i) / 999.0f;
            Color c = SurfaceSeries::sample_colormap(ColormapType::Viridis, t);
            benchmark::DoNotOptimize(c);
        }
    }
    state.SetItemsProcessed(state.iterations() * 1000);
}
BENCHMARK(BM_ColormapSample_Viridis);

static void BM_ColormapSample_Jet(benchmark::State& state)
{
    for (auto _ : state)
    {
        for (int i = 0; i < 1000; ++i)
        {
            float t = static_cast<float>(i) / 999.0f;
            Color c = SurfaceSeries::sample_colormap(ColormapType::Jet, t);
            benchmark::DoNotOptimize(c);
        }
    }
    state.SetItemsProcessed(state.iterations() * 1000);
}
BENCHMARK(BM_ColormapSample_Jet);

// ═══════════════════════════════════════════════════════════════════════════════
// 10. Camera Operations
// ═══════════════════════════════════════════════════════════════════════════════

static void BM_CameraOrbit_1000Steps(benchmark::State& state)
{
    Camera cam;
    cam.azimuth   = 0.0f;
    cam.elevation = 30.0f;
    cam.distance  = 5.0f;

    for (auto _ : state)
    {
        for (int i = 0; i < 1000; ++i)
        {
            cam.orbit(0.36f, 0.0f);
        }
        benchmark::DoNotOptimize(cam);
    }
    state.SetItemsProcessed(state.iterations() * 1000);
}
BENCHMARK(BM_CameraOrbit_1000Steps);

static void BM_CameraViewMatrix(benchmark::State& state)
{
    Camera cam;
    cam.azimuth   = 45.0f;
    cam.elevation = 30.0f;
    cam.distance  = 5.0f;
    cam.update_position_from_orbit();

    for (auto _ : state)
    {
        mat4 v = cam.view_matrix();
        benchmark::DoNotOptimize(v);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CameraViewMatrix);

static void BM_CameraProjectionMatrix(benchmark::State& state)
{
    Camera cam;
    cam.fov       = 45.0f;
    cam.near_clip = 0.01f;
    cam.far_clip  = 1000.0f;

    for (auto _ : state)
    {
        mat4 p = cam.projection_matrix(16.0f / 9.0f);
        benchmark::DoNotOptimize(p);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CameraProjectionMatrix);

static void BM_CameraSerialize(benchmark::State& state)
{
    Camera cam;
    cam.azimuth   = 123.0f;
    cam.elevation = 45.0f;
    cam.distance  = 7.5f;
    cam.fov       = 60.0f;

    for (auto _ : state)
    {
        std::string json = cam.serialize();
        benchmark::DoNotOptimize(json);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CameraSerialize);

static void BM_CameraDeserialize(benchmark::State& state)
{
    Camera cam;
    cam.azimuth      = 123.0f;
    cam.elevation    = 45.0f;
    cam.distance     = 7.5f;
    std::string json = cam.serialize();

    for (auto _ : state)
    {
        Camera restored;
        restored.deserialize(json);
        benchmark::DoNotOptimize(restored);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CameraDeserialize);

// ═══════════════════════════════════════════════════════════════════════════════
// 11. Data-to-Normalized Matrix
// ═══════════════════════════════════════════════════════════════════════════════

static void BM_DataToNormalizedMatrix(benchmark::State& state)
{
    App   app({.headless = true, .socket_path = ""});
    auto& fig = app.figure();
    auto& ax  = fig.subplot3d(1, 1, 1);
    ax.xlim(-5.0f, 5.0f);
    ax.ylim(-5.0f, 5.0f);
    ax.zlim(-5.0f, 5.0f);

    for (auto _ : state)
    {
        mat4 m = ax.data_to_normalized_matrix();
        benchmark::DoNotOptimize(m);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_DataToNormalizedMatrix);

// ═══════════════════════════════════════════════════════════════════════════════
// 12. Mixed 2D + 3D Rendering
// ═══════════════════════════════════════════════════════════════════════════════

static void BM_Mixed2D3D_LitSurface(benchmark::State& state)
{
    App   app({.headless = true, .socket_path = ""});
    auto& fig = app.figure({.width = 800, .height = 1200});

    auto&              ax2d = fig.subplot(2, 1, 1);
    std::vector<float> x2d(1000), y2d(1000);
    for (size_t i = 0; i < 1000; ++i)
    {
        x2d[i] = static_cast<float>(i) * 0.01f;
        y2d[i] = std::sin(x2d[i]);
    }
    ax2d.line(x2d, y2d).color(colors::blue);

    auto& ax3d = fig.subplot3d(2, 1, 2);
    auto  sg   = make_surface(50, 50, -3.0f, 3.0f, -3.0f, 3.0f);
    ax3d.surface(sg.x, sg.y, sg.z)
        .color(colors::orange)
        .ambient(0.2f)
        .specular(0.5f)
        .shininess(64.0f);

    for (auto _ : state)
    {
        app.run();
        benchmark::DoNotOptimize(ax2d);
        benchmark::DoNotOptimize(ax3d);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Mixed2D3D_LitSurface);

BENCHMARK_MAIN();
