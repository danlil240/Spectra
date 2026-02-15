#include <benchmark/benchmark.h>

#include <plotix/plotix.hpp>

#include <cmath>
#include <vector>

using namespace plotix;

static void BM_Scatter3D_1K(benchmark::State& state) {
    AppConfig config;
    config.headless = true;
    App app(config);
    auto& fig = app.figure({.width = 800, .height = 600});
    auto& ax = fig.subplot3d(1, 1, 1);
    
    std::vector<float> x(1000), y(1000), z(1000);
    for (size_t i = 0; i < 1000; ++i) {
        float t = static_cast<float>(i) * 0.01f;
        x[i] = std::cos(t);
        y[i] = std::sin(t);
        z[i] = t * 0.1f;
    }
    
    ax.scatter3d(x, y, z).color(colors::blue).size(4.0f);
    
    for (auto _ : state) {
        app.run();
        benchmark::DoNotOptimize(ax);
    }
    state.SetItemsProcessed(state.iterations() * 1000);
}
BENCHMARK(BM_Scatter3D_1K);

static void BM_Scatter3D_10K(benchmark::State& state) {
    AppConfig config;
    config.headless = true;
    App app(config);
    auto& fig = app.figure({.width = 800, .height = 600});
    auto& ax = fig.subplot3d(1, 1, 1);
    
    std::vector<float> x(10000), y(10000), z(10000);
    for (size_t i = 0; i < 10000; ++i) {
        float t = static_cast<float>(i) * 0.001f;
        x[i] = std::cos(t) * t;
        y[i] = std::sin(t) * t;
        z[i] = t;
    }
    
    ax.scatter3d(x, y, z).color(colors::red).size(3.0f);
    
    for (auto _ : state) {
        app.run();
        benchmark::DoNotOptimize(ax);
    }
    state.SetItemsProcessed(state.iterations() * 10000);
}
BENCHMARK(BM_Scatter3D_10K);

static void BM_Scatter3D_100K(benchmark::State& state) {
    AppConfig config;
    config.headless = true;
    App app(config);
    auto& fig = app.figure({.width = 800, .height = 600});
    auto& ax = fig.subplot3d(1, 1, 1);
    
    std::vector<float> x(100000), y(100000), z(100000);
    for (size_t i = 0; i < 100000; ++i) {
        float t = static_cast<float>(i) * 0.0001f;
        x[i] = std::cos(t) * std::sqrt(t);
        y[i] = std::sin(t) * std::sqrt(t);
        z[i] = t;
    }
    
    ax.scatter3d(x, y, z).color(colors::green).size(2.0f);
    
    for (auto _ : state) {
        app.run();
        benchmark::DoNotOptimize(ax);
    }
    state.SetItemsProcessed(state.iterations() * 100000);
}
BENCHMARK(BM_Scatter3D_100K);

static void BM_Scatter3D_500K(benchmark::State& state) {
    AppConfig config;
    config.headless = true;
    App app(config);
    auto& fig = app.figure({.width = 800, .height = 600});
    auto& ax = fig.subplot3d(1, 1, 1);
    
    std::vector<float> x(500000), y(500000), z(500000);
    for (size_t i = 0; i < 500000; ++i) {
        float t = static_cast<float>(i) * 0.00002f;
        x[i] = std::cos(t * 2.0f) * (1.0f + t);
        y[i] = std::sin(t * 2.0f) * (1.0f + t);
        z[i] = t;
    }
    
    ax.scatter3d(x, y, z).color(colors::cyan).size(2.0f);
    
    for (auto _ : state) {
        app.run();
        benchmark::DoNotOptimize(ax);
    }
    state.SetItemsProcessed(state.iterations() * 500000);
}
BENCHMARK(BM_Scatter3D_500K);

static void BM_Line3D_1K(benchmark::State& state) {
    AppConfig config;
    config.headless = true;
    App app(config);
    auto& fig = app.figure({.width = 800, .height = 600});
    auto& ax = fig.subplot3d(1, 1, 1);
    
    std::vector<float> x(1000), y(1000), z(1000);
    for (size_t i = 0; i < 1000; ++i) {
        float t = static_cast<float>(i) * 0.01f;
        x[i] = std::cos(t);
        y[i] = std::sin(t);
        z[i] = t * 0.1f;
    }
    
    ax.line3d(x, y, z).color(colors::blue).width(2.0f);
    
    for (auto _ : state) {
        app.run();
        benchmark::DoNotOptimize(ax);
    }
    state.SetItemsProcessed(state.iterations() * 1000);
}
BENCHMARK(BM_Line3D_1K);

static void BM_Line3D_50K(benchmark::State& state) {
    AppConfig config;
    config.headless = true;
    App app(config);
    auto& fig = app.figure({.width = 800, .height = 600});
    auto& ax = fig.subplot3d(1, 1, 1);
    
    std::vector<float> x(50000), y(50000), z(50000);
    for (size_t i = 0; i < 50000; ++i) {
        float t = static_cast<float>(i) * 0.0002f;
        x[i] = std::cos(t) * t;
        y[i] = std::sin(t) * t;
        z[i] = t;
    }
    
    ax.line3d(x, y, z).color(colors::magenta).width(2.5f);
    
    for (auto _ : state) {
        app.run();
        benchmark::DoNotOptimize(ax);
    }
    state.SetItemsProcessed(state.iterations() * 50000);
}
BENCHMARK(BM_Line3D_50K);

static void BM_Surface_50x50(benchmark::State& state) {
    AppConfig config;
    config.headless = true;
    App app(config);
    auto& fig = app.figure({.width = 800, .height = 600});
    auto& ax = fig.subplot3d(1, 1, 1);
    
    const int nx = 50, ny = 50;
    std::vector<float> x_grid(nx), y_grid(ny), z_values(nx * ny);
    
    for (int i = 0; i < nx; ++i) {
        x_grid[i] = static_cast<float>(i) / (nx - 1) * 4.0f - 2.0f;
    }
    for (int j = 0; j < ny; ++j) {
        y_grid[j] = static_cast<float>(j) / (ny - 1) * 4.0f - 2.0f;
    }
    
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            float x = x_grid[i];
            float y = y_grid[j];
            z_values[j * nx + i] = std::sin(x) * std::cos(y);
        }
    }
    
    ax.surface(x_grid, y_grid, z_values).color(colors::orange);
    
    for (auto _ : state) {
        app.run();
        benchmark::DoNotOptimize(ax);
    }
    state.SetItemsProcessed(state.iterations() * (nx * ny));
}
BENCHMARK(BM_Surface_50x50);

static void BM_Surface_100x100(benchmark::State& state) {
    AppConfig config;
    config.headless = true;
    App app(config);
    auto& fig = app.figure({.width = 800, .height = 600});
    auto& ax = fig.subplot3d(1, 1, 1);
    
    const int nx = 100, ny = 100;
    std::vector<float> x_grid(nx), y_grid(ny), z_values(nx * ny);
    
    for (int i = 0; i < nx; ++i) {
        x_grid[i] = static_cast<float>(i) / (nx - 1) * 6.0f - 3.0f;
    }
    for (int j = 0; j < ny; ++j) {
        y_grid[j] = static_cast<float>(j) / (ny - 1) * 6.0f - 3.0f;
    }
    
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            float x = x_grid[i];
            float y = y_grid[j];
            z_values[j * nx + i] = std::sin(std::sqrt(x*x + y*y));
        }
    }
    
    ax.surface(x_grid, y_grid, z_values).color(colors::yellow);
    
    for (auto _ : state) {
        app.run();
        benchmark::DoNotOptimize(ax);
    }
    state.SetItemsProcessed(state.iterations() * (nx * ny));
}
BENCHMARK(BM_Surface_100x100);

static void BM_Surface_500x500(benchmark::State& state) {
    AppConfig config;
    config.headless = true;
    App app(config);
    auto& fig = app.figure({.width = 800, .height = 600});
    auto& ax = fig.subplot3d(1, 1, 1);
    
    const int nx = 500, ny = 500;
    std::vector<float> x_grid(nx), y_grid(ny), z_values(nx * ny);
    
    for (int i = 0; i < nx; ++i) {
        x_grid[i] = static_cast<float>(i) / (nx - 1) * 10.0f - 5.0f;
    }
    for (int j = 0; j < ny; ++j) {
        y_grid[j] = static_cast<float>(j) / (ny - 1) * 10.0f - 5.0f;
    }
    
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            float x = x_grid[i];
            float y = y_grid[j];
            float r = std::sqrt(x*x + y*y);
            z_values[j * nx + i] = std::sin(r) / (r + 0.1f);
        }
    }
    
    ax.surface(x_grid, y_grid, z_values).color(colors::red);
    
    for (auto _ : state) {
        app.run();
        benchmark::DoNotOptimize(ax);
    }
    state.SetItemsProcessed(state.iterations() * (nx * ny));
}
BENCHMARK(BM_Surface_500x500);

static void BM_Mixed2DAnd3D(benchmark::State& state) {
    AppConfig config;
    config.headless = true;
    App app(config);
    auto& fig = app.figure({.width = 800, .height = 1200});
    
    auto& ax2d = fig.subplot(2, 1, 1);
    std::vector<float> x2d(1000), y2d(1000);
    for (size_t i = 0; i < 1000; ++i) {
        x2d[i] = static_cast<float>(i) * 0.01f;
        y2d[i] = std::sin(x2d[i]);
    }
    ax2d.line(x2d, y2d).color(colors::blue);
    
    auto& ax3d = fig.subplot3d(2, 1, 2);
    std::vector<float> x3d(5000), y3d(5000), z3d(5000);
    for (size_t i = 0; i < 5000; ++i) {
        float t = static_cast<float>(i) * 0.002f;
        x3d[i] = std::cos(t);
        y3d[i] = std::sin(t);
        z3d[i] = t * 0.1f;
    }
    ax3d.scatter3d(x3d, y3d, z3d).color(colors::red);
    
    for (auto _ : state) {
        app.run();
        benchmark::DoNotOptimize(ax2d);
        benchmark::DoNotOptimize(ax3d);
    }
    state.SetItemsProcessed(state.iterations() * 6000);
}
BENCHMARK(BM_Mixed2DAnd3D);

static void BM_CameraOrbit_1000Frames(benchmark::State& state) {
    AppConfig config;
    config.headless = true;
    App app(config);
    auto& fig = app.figure({.width = 800, .height = 600});
    auto& ax = fig.subplot3d(1, 1, 1);
    
    std::vector<float> x(100), y(100), z(100);
    for (size_t i = 0; i < 100; ++i) {
        float t = static_cast<float>(i) * 0.1f;
        x[i] = std::cos(t);
        y[i] = std::sin(t);
        z[i] = t * 0.1f;
    }
    ax.scatter3d(x, y, z).color(colors::green);
    
    for (auto _ : state) {
        for (int frame = 0; frame < 1000; ++frame) {
            ax.camera().orbit(0.36f, 0.0f);
            app.run();
        }
        benchmark::DoNotOptimize(ax);
    }
    state.SetItemsProcessed(state.iterations() * 1000);
}
BENCHMARK(BM_CameraOrbit_1000Frames);

BENCHMARK_MAIN();
