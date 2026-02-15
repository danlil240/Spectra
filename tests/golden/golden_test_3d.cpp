#include <gtest/gtest.h>

#include <plotix/plotix.hpp>

#include "image_diff.hpp"
#include "render/backend.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace plotix::test {

static fs::path baseline_dir() {
    if (const char* env = std::getenv("PLOTIX_GOLDEN_BASELINE_DIR")) {
        return fs::path(env);
    }
    return fs::path(__FILE__).parent_path() / "baseline";
}

static fs::path output_dir() {
    if (const char* env = std::getenv("PLOTIX_GOLDEN_OUTPUT_DIR")) {
        return fs::path(env);
    }
    return fs::path(__FILE__).parent_path() / "output";
}

static bool update_baselines() {
    const char* env = std::getenv("PLOTIX_UPDATE_BASELINES");
    return env && std::string(env) == "1";
}

static bool render_headless(Figure& fig, App& app, std::vector<uint8_t>& pixels) {
    uint32_t w = fig.width();
    uint32_t h = fig.height();

    app.run();

    pixels.resize(static_cast<size_t>(w) * h * 4);
    Backend* backend = app.backend();
    if (!backend) return false;

    return backend->readback_framebuffer(pixels.data(), w, h);
}

static void run_golden_test_3d(const std::string& scene_name,
                                std::function<void(App&, Figure&)> setup_scene,
                                uint32_t width = 640, uint32_t height = 480,
                                double tolerance_percent = 2.0,
                                double max_mae = 3.0) {
    fs::path baseline_path = baseline_dir() / (scene_name + ".raw");
    fs::path actual_path   = output_dir() / (scene_name + "_actual.raw");
    fs::path diff_path     = output_dir() / (scene_name + "_diff.raw");

    fs::create_directories(output_dir());

    App app({.headless = true});
    auto& fig = app.figure({.width = width, .height = height});

    setup_scene(app, fig);

    std::vector<uint8_t> actual_pixels;
    ASSERT_TRUE(render_headless(fig, app, actual_pixels))
        << "Failed to render scene: " << scene_name;

    ASSERT_TRUE(save_raw_rgba(actual_path.string(), actual_pixels.data(), width, height))
        << "Failed to save actual render for: " << scene_name;

    if (update_baselines()) {
        fs::create_directories(baseline_dir());
        ASSERT_TRUE(save_raw_rgba(baseline_path.string(), actual_pixels.data(), width, height))
            << "Failed to save baseline for: " << scene_name;
        std::cout << "[GOLDEN 3D] Updated baseline: " << baseline_path << "\n";
        return;
    }

    if (!fs::exists(baseline_path)) {
        GTEST_SKIP() << "Baseline not found: " << baseline_path
                     << " (run with PLOTIX_UPDATE_BASELINES=1 to generate)";
        return;
    }

    std::vector<uint8_t> baseline_pixels;
    uint32_t baseline_w, baseline_h;
    ASSERT_TRUE(load_raw_rgba(baseline_path.string(), baseline_pixels, baseline_w, baseline_h))
        << "Failed to load baseline: " << baseline_path;

    ASSERT_EQ(baseline_w, width) << "Baseline width mismatch for: " << scene_name;
    ASSERT_EQ(baseline_h, height) << "Baseline height mismatch for: " << scene_name;

    DiffResult diff = compare_images(
        actual_pixels.data(), baseline_pixels.data(), width, height);

    std::vector<uint8_t> diff_pixels = generate_diff_image(
        actual_pixels.data(), baseline_pixels.data(), width, height);
    save_raw_rgba(diff_path.string(), diff_pixels.data(), width, height);

    EXPECT_LE(diff.percent_different, tolerance_percent)
        << "Scene: " << scene_name
        << "\n  MAE: " << diff.mean_absolute_error
        << "\n  Max error: " << diff.max_absolute_error
        << "\n  Different pixels: " << diff.percent_different << "%"
        << "\n  Diff image: " << diff_path;

    EXPECT_LE(diff.mean_absolute_error, max_mae)
        << "Scene: " << scene_name << " has high mean absolute error";
}

TEST(Golden3D, Scatter3D_Basic) {
    run_golden_test_3d("3d_scatter_basic", [](App& app, Figure& fig) {
        auto& ax = fig.subplot3d(1, 1, 1);
        
        std::vector<float> x = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f};
        std::vector<float> y = {0.0f, 1.0f, 0.5f, 1.5f, 1.0f};
        std::vector<float> z = {0.0f, 0.5f, 1.0f, 0.5f, 0.0f};
        
        ax.scatter3d(x, y, z).color(colors::blue).size(8.0f);
        ax.title("3D Scatter Plot");
        ax.xlabel("X Axis");
        ax.ylabel("Y Axis");
        ax.zlabel("Z Axis");
    });
}

TEST(Golden3D, Scatter3D_LargeDataset) {
    run_golden_test_3d("3d_scatter_large", [](App& app, Figure& fig) {
        auto& ax = fig.subplot3d(1, 1, 1);
        
        std::vector<float> x, y, z;
        for (int i = 0; i < 1000; ++i) {
            float t = static_cast<float>(i) * 0.01f;
            x.push_back(std::cos(t) * t);
            y.push_back(std::sin(t) * t);
            z.push_back(t);
        }
        
        ax.scatter3d(x, y, z).color(colors::red).size(3.0f);
        ax.title("Spiral Scatter");
    });
}

TEST(Golden3D, Line3D_Basic) {
    run_golden_test_3d("3d_line_basic", [](App& app, Figure& fig) {
        auto& ax = fig.subplot3d(1, 1, 1);
        
        std::vector<float> x = {0.0f, 1.0f, 2.0f, 3.0f};
        std::vector<float> y = {0.0f, 1.0f, 0.0f, 1.0f};
        std::vector<float> z = {0.0f, 0.0f, 1.0f, 1.0f};
        
        ax.line3d(x, y, z).color(colors::green).width(3.0f);
        ax.title("3D Line Plot");
    });
}

TEST(Golden3D, Line3D_Helix) {
    run_golden_test_3d("3d_line_helix", [](App& app, Figure& fig) {
        auto& ax = fig.subplot3d(1, 1, 1);
        
        std::vector<float> x, y, z;
        for (int i = 0; i < 200; ++i) {
            float t = static_cast<float>(i) * 0.1f;
            x.push_back(std::cos(t));
            y.push_back(std::sin(t));
            z.push_back(t * 0.1f);
        }
        
        ax.line3d(x, y, z).color(colors::cyan).width(2.5f);
        ax.title("Helix");
    });
}

TEST(Golden3D, Surface_Basic) {
    run_golden_test_3d("3d_surface_basic", [](App& app, Figure& fig) {
        auto& ax = fig.subplot3d(1, 1, 1);
        
        std::vector<float> x_grid, y_grid, z_values;
        const int nx = 20, ny = 20;
        
        for (int i = 0; i < nx; ++i) {
            x_grid.push_back(static_cast<float>(i) / (nx - 1) * 4.0f - 2.0f);
        }
        for (int j = 0; j < ny; ++j) {
            y_grid.push_back(static_cast<float>(j) / (ny - 1) * 4.0f - 2.0f);
        }
        
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                float x = x_grid[i];
                float y = y_grid[j];
                float z = std::sin(x) * std::cos(y);
                z_values.push_back(z);
            }
        }
        
        ax.surface(x_grid, y_grid, z_values).color(colors::orange);
        ax.title("Surface: sin(x)*cos(y)");
    });
}

TEST(Golden3D, BoundingBox) {
    run_golden_test_3d("3d_bounding_box", [](App& app, Figure& fig) {
        auto& ax = fig.subplot3d(1, 1, 1);
        
        ax.xlim(-1.0f, 1.0f);
        ax.ylim(-1.0f, 1.0f);
        ax.zlim(-1.0f, 1.0f);
        ax.show_bounding_box(true);
        ax.title("Bounding Box Only");
    });
}

TEST(Golden3D, GridPlanes_XY) {
    run_golden_test_3d("3d_grid_xy", [](App& app, Figure& fig) {
        auto& ax = fig.subplot3d(1, 1, 1);
        
        ax.set_grid_planes(static_cast<int>(Axes3D::GridPlane::XY));
        
        std::vector<float> x = {0.0f, 1.0f};
        std::vector<float> y = {0.0f, 1.0f};
        std::vector<float> z = {0.0f, 1.0f};
        ax.scatter3d(x, y, z).color(colors::blue);
        
        ax.title("XY Grid Plane");
    });
}

TEST(Golden3D, GridPlanes_All) {
    run_golden_test_3d("3d_grid_all", [](App& app, Figure& fig) {
        auto& ax = fig.subplot3d(1, 1, 1);
        
        ax.set_grid_planes(static_cast<int>(Axes3D::GridPlane::All));
        
        std::vector<float> x = {0.5f};
        std::vector<float> y = {0.5f};
        std::vector<float> z = {0.5f};
        ax.scatter3d(x, y, z).color(colors::red).size(10.0f);
        
        ax.title("All Grid Planes");
    });
}

TEST(Golden3D, CameraAngle_Front) {
    run_golden_test_3d("3d_camera_front", [](App& app, Figure& fig) {
        auto& ax = fig.subplot3d(1, 1, 1);
        
        std::vector<float> x = {0.0f, 1.0f, 0.5f};
        std::vector<float> y = {0.0f, 0.0f, 1.0f};
        std::vector<float> z = {0.0f, 1.0f, 0.5f};
        ax.scatter3d(x, y, z).color(colors::magenta).size(8.0f);
        
        ax.camera().azimuth = 0.0f;
        ax.camera().elevation = 0.0f;
        ax.camera().distance = 5.0f;
        
        ax.title("Front View");
    });
}

TEST(Golden3D, CameraAngle_Top) {
    run_golden_test_3d("3d_camera_top", [](App& app, Figure& fig) {
        auto& ax = fig.subplot3d(1, 1, 1);
        
        std::vector<float> x = {0.0f, 1.0f, 0.5f};
        std::vector<float> y = {0.0f, 0.0f, 1.0f};
        std::vector<float> z = {0.0f, 1.0f, 0.5f};
        ax.scatter3d(x, y, z).color(colors::yellow).size(8.0f);
        
        ax.camera().azimuth = 0.0f;
        ax.camera().elevation = 90.0f;
        ax.camera().distance = 5.0f;
        
        ax.title("Top View");
    });
}

TEST(Golden3D, DepthOcclusion) {
    run_golden_test_3d("3d_depth_occlusion", [](App& app, Figure& fig) {
        auto& ax = fig.subplot3d(1, 1, 1);
        
        std::vector<float> x_front = {0.0f};
        std::vector<float> y_front = {0.0f};
        std::vector<float> z_front = {1.0f};
        
        std::vector<float> x_back = {0.0f};
        std::vector<float> y_back = {0.0f};
        std::vector<float> z_back = {-1.0f};
        
        ax.scatter3d(x_back, y_back, z_back).color(colors::blue).size(20.0f);
        ax.scatter3d(x_front, y_front, z_front).color(colors::red).size(15.0f);
        
        ax.title("Depth Test: Red in Front");
    });
}

TEST(Golden3D, Mixed2DAnd3D) {
    run_golden_test_3d("3d_mixed_2d_3d", [](App& app, Figure& fig) {
        auto& ax2d = fig.subplot(2, 1, 1);
        std::vector<float> x2d = {0.0f, 1.0f, 2.0f, 3.0f};
        std::vector<float> y2d = {0.0f, 1.0f, 0.5f, 1.5f};
        ax2d.line(x2d, y2d).color(colors::green);
        ax2d.title("2D Line");
        
        auto& ax3d = fig.subplot3d(2, 1, 2);
        std::vector<float> x3d = {0.0f, 1.0f, 2.0f};
        std::vector<float> y3d = {0.0f, 1.0f, 0.5f};
        std::vector<float> z3d = {0.0f, 0.5f, 1.0f};
        ax3d.scatter3d(x3d, y3d, z3d).color(colors::blue);
        ax3d.title("3D Scatter");
    }, 640, 960);
}

} // namespace plotix::test
