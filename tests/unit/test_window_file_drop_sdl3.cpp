// test_window_file_drop_sdl3.cpp — OS file drop end to end through SDL3.
//
// SDL3 builds only (SPECTRA_USE_SDL3, mutually exclusive with GLFW): pushes an
// SDL_EVENT_DROP_FILE for the App's window into SDL's queue, steps the App
// (WindowManager polls SDL events and routes the drop through the dispatch
// shared with GLFW) and checks the dropped CSV opened as data.
// Needs an X display and a Vulkan device (xvfb-run, label gpu); skips otherwise.

#include <gtest/gtest.h>

#include <spectra/app.hpp>
#include <spectra/figure.hpp>

#include <SDL3/SDL.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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

}   // namespace

TEST(Sdl3FileDrop, DroppedCsvOpensAsData)
{
    if (!std::getenv("DISPLAY"))
        GTEST_SKIP() << "No X display";
    const fs::path dir = fs::temp_directory_path() / "spectra_sdl3_file_drop";
    fs::create_directories(dir);
    const fs::path csv = dir / "speed.csv";
    std::ofstream(csv) << "t,speed\n0,1.5\n1,2.5\n2,4.0\n";

    {
        App app({.headless = false, .socket_path = ""});
        if (!app.ok())
            GTEST_SKIP() << "No Vulkan device / window";
        app.figure({.width = 640, .height = 480});
        app.init_runtime();
        auto* wm = app.window_manager();
        auto* ui = app.ui_context();
        if (!wm || wm->windows().empty() || !ui || !ui->imgui_ui)
            GTEST_SKIP() << "No window";
        auto* win = static_cast<SDL_Window*>(wm->windows()[0]->glfw_window);
        ASSERT_NE(win, nullptr);
        step(app, 2);

        // What SDL queues for an OS drop onto the window.
        const std::string path = csv.string();
        SDL_Event         ev{};
        ev.type          = SDL_EVENT_DROP_FILE;
        ev.drop.windowID = SDL_GetWindowID(win);
        ev.drop.data     = path.c_str();
        ASSERT_TRUE(SDL_PushEvent(&ev)) << SDL_GetError();
        step(app, 3);

        ImGuiIntegration& imgui = *ui->imgui_ui;
        ASSERT_TRUE(imgui.csv_dialog_open()) << "SDL drop did not open the column picker";
        EXPECT_EQ(imgui.csv_file_path(), path);
        ASSERT_EQ(imgui.csv_data().num_rows, 3u);
        EXPECT_EQ(imgui.csv_data().columns[1], (std::vector<float>{1.5f, 2.5f, 4.0f}));
        app.shutdown_runtime();
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}
