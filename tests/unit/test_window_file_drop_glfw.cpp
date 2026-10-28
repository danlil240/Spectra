// test_window_file_drop_glfw.cpp — OS file drop end to end through GLFW.
//
// A windowed App installs WindowManager::glfw_drop_callback on its GLFW
// window.  The test fetches the callback GLFW actually holds for the window
// and calls it the way GLFW does for an OS drop, then steps the App and
// checks the dropped CSV opened as data: parsed into the column picker and,
// once plotted, a line series with the file's values.
//
// Needs an X display and a Vulkan device (registered under xvfb-run, label
// gpu); skips without them.

#include <gtest/gtest.h>

#include <spectra/app.hpp>
#include <spectra/export.hpp>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>

#include <GLFW/glfw3.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "render/backend.hpp"
#include "render/vulkan/window_context.hpp"
#include "ui/app/session_runtime.hpp"
#include "ui/app/window_ui_context.hpp"
#include "ui/imgui/imgui_integration.hpp"
#include "ui/window/window_manager.hpp"

namespace fs = std::filesystem;
using namespace spectra;

namespace
{

void step(App& app, int frames)
{
    for (int i = 0; i < frames; ++i)
    {
        if (auto* s = app.session())
            s->redraw_tracker().mark_dirty("test");
        app.step();
    }
}

// Visual evidence: with SPECTRA_TEST_CAPTURE_DIR set, saves the window
// framebuffer as <dir>/<name>.png.  A no-op otherwise.
void capture(App& app, const std::string& name)
{
    const char* dir = std::getenv("SPECTRA_TEST_CAPTURE_DIR");
    if (!dir || !*dir || !app.backend())
        return;
    const uint32_t w = app.backend()->swapchain_width();
    const uint32_t h = app.backend()->swapchain_height();
    if (w == 0 || h == 0)
        return;
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    if (app.backend()->readback_framebuffer(px.data(), w, h))
        ImageExporter::write_png((fs::path(dir) / (name + ".png")).string(), px.data(), w, h);
}

// Calls the drop callback installed on `win`, as GLFW does for an OS drop.
bool glfw_drop(GLFWwindow* win, const std::vector<std::string>& paths)
{
    GLFWdropfun cb = glfwSetDropCallback(win, nullptr);
    glfwSetDropCallback(win, cb);
    if (!cb)
        return false;
    std::vector<const char*> c_paths;
    for (const auto& p : paths)
        c_paths.push_back(p.c_str());
    cb(win, static_cast<int>(c_paths.size()), c_paths.data());
    return true;
}

// Lines of all figures, as (label, y values).
std::vector<const LineSeries*> all_lines(App& app)
{
    std::vector<const LineSeries*> out;
    for (FigureId id : app.figure_registry().all_ids())
    {
        Figure* fig = app.figure_registry().get(id);
        if (!fig)
            continue;
        for (const auto& ax : fig->axes())
        {
            if (!ax)
                continue;
            for (const auto& s : ax->series())
                if (auto* l = dynamic_cast<const LineSeries*>(s.get()))
                    out.push_back(l);
        }
    }
    return out;
}

class GlfwFileDropTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        if (!std::getenv("DISPLAY"))
            GTEST_SKIP() << "No X display";
        dir_ = fs::temp_directory_path() / "spectra_glfw_file_drop";
        fs::create_directories(dir_);
    }
    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }

    fs::path write(const std::string& name, const std::string& text)
    {
        const fs::path p = dir_ / name;
        std::ofstream(p) << text;
        return p;
    }

    fs::path dir_;
};

}   // namespace

TEST_F(GlfwFileDropTest, DroppedCsvOpensAndPlotsItsValues)
{
    const fs::path a = write("speed.csv", "t,speed,alt\n0,1.5,100\n1,2.5,110\n2,4.0,125\n");
    const fs::path b = write("second.csv", "x,y\n0,-1\n1,-2\n");

    App app({.headless = false, .socket_path = ""});
    if (!app.ok())
        GTEST_SKIP() << "No Vulkan device / window";
    app.figure({.width = 640, .height = 480});
    app.init_runtime();
    auto* wm = app.window_manager();
    auto* ui = app.ui_context();
    if (!wm || wm->windows().empty() || !ui || !ui->imgui_ui)
        GTEST_SKIP() << "No window";
    auto* win = static_cast<GLFWwindow*>(wm->windows()[0]->glfw_window);
    ASSERT_NE(win, nullptr);
    step(app, 2);
    const size_t lines_before = all_lines(app).size();

    // One OS drop carrying two files.
    ASSERT_TRUE(glfw_drop(win, {a.string(), b.string()})) << "no GLFW drop callback installed";
    step(app, 2);
    capture(app, "glfw_drop_csv_column_picker");

    ImGuiIntegration& imgui = *ui->imgui_ui;
    ASSERT_TRUE(imgui.csv_dialog_open()) << "column picker not opened by the drop";
    EXPECT_EQ(imgui.csv_file_path(), a.string());
    EXPECT_TRUE(imgui.csv_error().empty()) << imgui.csv_error();
    const CsvData& data = imgui.csv_data();
    ASSERT_EQ(data.num_cols, 3u);
    ASSERT_EQ(data.num_rows, 3u);
    EXPECT_EQ(data.headers, (std::vector<std::string>{"t", "speed", "alt"}));
    EXPECT_EQ(data.columns[1], (std::vector<float>{1.5f, 2.5f, 4.0f}));
    EXPECT_EQ(imgui.pending_csv_load_count(), 1u) << "second dropped file waits its turn";

    // Plot (the picker's "Plot" button): one line per selected y column.
    ASSERT_TRUE(imgui.plot_csv_selection());
    step(app, 2);
    capture(app, "glfw_drop_csv_plotted_then_second_file");
    auto lines = all_lines(app);
    ASSERT_EQ(lines.size(), lines_before + 2);
    const LineSeries* speed = nullptr;
    for (auto* l : lines)
        if (l->label() == "speed")
            speed = l;
    ASSERT_NE(speed, nullptr);
    ASSERT_EQ(speed->y_data().size(), 3u);
    EXPECT_FLOAT_EQ(speed->y_data()[2], 4.0f);
    EXPECT_DOUBLE_EQ(speed->x_offset() + speed->x_data()[2], 2.0);

    // The second file of the drop opens next.
    ASSERT_TRUE(imgui.csv_dialog_open());
    EXPECT_EQ(imgui.csv_file_path(), b.string());
    EXPECT_EQ(imgui.csv_data().columns[1], (std::vector<float>{-1.0f, -2.0f}));
    EXPECT_EQ(imgui.pending_csv_load_count(), 0u);
    app.shutdown_runtime();
}

TEST_F(GlfwFileDropTest, UnreadableCsvDropShowsTheError)
{
    const fs::path bad = write("empty.csv", "");

    App app({.headless = false, .socket_path = ""});
    if (!app.ok())
        GTEST_SKIP() << "No Vulkan device / window";
    app.figure({.width = 640, .height = 480});
    app.init_runtime();
    auto* wm = app.window_manager();
    auto* ui = app.ui_context();
    if (!wm || wm->windows().empty() || !ui || !ui->imgui_ui)
        GTEST_SKIP() << "No window";
    auto* win = static_cast<GLFWwindow*>(wm->windows()[0]->glfw_window);
    step(app, 2);

    ASSERT_TRUE(glfw_drop(win, {bad.string()}));
    step(app, 2);
    capture(app, "glfw_drop_unreadable_csv_error");
    ImGuiIntegration& imgui = *ui->imgui_ui;
    // Used to fail silently: the picker only opened for files that parsed.
    EXPECT_TRUE(imgui.csv_dialog_open()) << "parse error not shown";
    EXPECT_FALSE(imgui.csv_error().empty());
    EXPECT_FALSE(imgui.plot_csv_selection());
    app.shutdown_runtime();
}

TEST_F(GlfwFileDropTest, NonCsvDropIsIgnored)
{
    const fs::path other = write("flight.ulg", "ULog");

    App app({.headless = false, .socket_path = ""});
    if (!app.ok())
        GTEST_SKIP() << "No Vulkan device / window";
    app.figure({.width = 640, .height = 480});
    app.init_runtime();
    auto* wm = app.window_manager();
    auto* ui = app.ui_context();
    if (!wm || wm->windows().empty() || !ui || !ui->imgui_ui)
        GTEST_SKIP() << "No window";
    auto* win = static_cast<GLFWwindow*>(wm->windows()[0]->glfw_window);
    step(app, 2);
    ASSERT_TRUE(glfw_drop(win, {other.string()}));
    step(app, 2);
    EXPECT_FALSE(ui->imgui_ui->csv_dialog_open());
    EXPECT_EQ(ui->imgui_ui->pending_csv_load_count(), 0u);
    app.shutdown_runtime();
}
