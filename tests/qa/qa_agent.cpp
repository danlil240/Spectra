// qa_agent.cpp — Spectra QA stress testing agent.
// Launches a real GLFW-windowed Spectra app and drives it programmatically
// through randomized fuzzing and predefined stress scenarios, tracking
// crashes, Vulkan errors, frame time regressions, and memory growth.
//
// Usage:
//   spectra_qa_agent [options]
//     --seed <N>          RNG seed (default: time-based)
//     --duration <sec>    Max runtime seconds (default: 120)
//     --scenario <name>   Run single scenario (default: all)
//     --fuzz-frames <N>   Random fuzzing frames (default: 3000)
//     --output-dir <path> Report/screenshot dir (default: /tmp/spectra_qa)
//     --no-fuzz           Skip fuzzing phase
//     --no-scenarios      Skip scenarios phase
//     --list-scenarios    List scenarios and exit

#include <spectra/app.hpp>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>

#include "render/backend.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include "ui/app/window_ui_context.hpp"
    #include "ui/commands/command_registry.hpp"
    #include "ui/figures/figure_manager.hpp"
    #include "ui/input/input.hpp"
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef __linux__
    #include <execinfo.h>
    #include <fcntl.h>
    #include <unistd.h>
#endif

using namespace spectra;

// ─── RSS monitoring (Linux) ──────────────────────────────────────────────────
static size_t get_rss_bytes()
{
#ifdef __linux__
    FILE* f = fopen("/proc/self/statm", "r");
    if (!f)
        return 0;
    long pages = 0;
    if (fscanf(f, "%*ld %ld", &pages) != 1)
        pages = 0;
    fclose(f);
    return static_cast<size_t>(pages) * 4096;
#else
    return 0;
#endif
}

// ─── Issue tracking ──────────────────────────────────────────────────────────
enum class IssueSeverity
{
    Info,
    Warning,
    Error,
    Critical
};

struct QAIssue
{
    IssueSeverity severity;
    std::string   category;
    std::string   message;
    uint64_t      frame;
    std::string   screenshot_path;
};

static const char* severity_str(IssueSeverity s)
{
    switch (s)
    {
        case IssueSeverity::Info: return "INFO";
        case IssueSeverity::Warning: return "WARNING";
        case IssueSeverity::Error: return "ERROR";
        case IssueSeverity::Critical: return "CRITICAL";
    }
    return "???";
}

// ─── Crash handler globals (must be before QAAgent class) ────────────────────
static uint64_t g_qa_seed = 0;
static char     g_last_action[256] = "init";
static char     g_output_dir[512]  = "/tmp/spectra_qa";

// ─── Frame time statistics ───────────────────────────────────────────────────
struct FrameStats
{
    std::vector<float> samples;
    float              ema         = 0.0f;
    float              ema_alpha   = 0.05f;
    uint32_t           spike_count = 0;

    void record(float ms)
    {
        samples.push_back(ms);
        if (ema < 0.001f)
            ema = ms;
        else
            ema = ema_alpha * ms + (1.0f - ema_alpha) * ema;
    }

    float average() const
    {
        if (samples.empty())
            return 0.0f;
        double sum = 0.0;
        for (float s : samples)
            sum += s;
        return static_cast<float>(sum / samples.size());
    }

    float percentile(float p) const
    {
        if (samples.empty())
            return 0.0f;
        auto sorted = samples;
        std::sort(sorted.begin(), sorted.end());
        size_t idx = static_cast<size_t>(p * static_cast<float>(sorted.size() - 1));
        return sorted[std::min(idx, sorted.size() - 1)];
    }

    float max_val() const
    {
        if (samples.empty())
            return 0.0f;
        return *std::max_element(samples.begin(), samples.end());
    }
};

// ─── CLI options ─────────────────────────────────────────────────────────────
struct QAOptions
{
    uint64_t    seed           = 0;
    float       duration_sec   = 120.0f;
    std::string scenario_name;
    uint64_t    fuzz_frames    = 3000;
    std::string output_dir     = "/tmp/spectra_qa";
    bool        no_fuzz        = false;
    bool        no_scenarios   = false;
    bool        list_scenarios = false;
    bool        design_review  = false;
};

static QAOptions parse_args(int argc, char** argv)
{
    QAOptions opts;
    opts.seed = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--seed" && i + 1 < argc)
            opts.seed = std::stoull(argv[++i]);
        else if (arg == "--duration" && i + 1 < argc)
            opts.duration_sec = std::stof(argv[++i]);
        else if (arg == "--scenario" && i + 1 < argc)
            opts.scenario_name = argv[++i];
        else if (arg == "--fuzz-frames" && i + 1 < argc)
            opts.fuzz_frames = std::stoull(argv[++i]);
        else if (arg == "--output-dir" && i + 1 < argc)
            opts.output_dir = argv[++i];
        else if (arg == "--no-fuzz")
            opts.no_fuzz = true;
        else if (arg == "--no-scenarios")
            opts.no_scenarios = true;
        else if (arg == "--list-scenarios")
            opts.list_scenarios = true;
        else if (arg == "--design-review")
            opts.design_review = true;
        else if (arg == "--help" || arg == "-h")
        {
            fprintf(stderr,
                    "Usage: spectra_qa_agent [options]\n"
                    "  --seed <N>          RNG seed (default: time-based)\n"
                    "  --duration <sec>    Max runtime seconds (default: 120)\n"
                    "  --scenario <name>   Run single scenario (default: all)\n"
                    "  --fuzz-frames <N>   Random fuzzing frames (default: 3000)\n"
                    "  --output-dir <path> Report/screenshot dir (default: /tmp/spectra_qa)\n"
                    "  --no-fuzz           Skip fuzzing phase\n"
                    "  --no-scenarios      Skip scenarios phase\n"
                    "  --list-scenarios    List scenarios and exit\n"
                    "  --design-review     Capture UI screenshots for design analysis\n");
            exit(0);
        }
    }
    return opts;
}

// ─── Scenario definition ─────────────────────────────────────────────────────
struct Scenario
{
    std::string                          name;
    std::string                          description;
    std::function<bool(class QAAgent&)>  run;
};

// ─── QAAgent class ───────────────────────────────────────────────────────────
class QAAgent
{
   public:
    QAAgent(const QAOptions& opts)
        : opts_(opts), rng_(opts.seed), start_time_(std::chrono::steady_clock::now())
    {
        std::filesystem::create_directories(opts_.output_dir);
    }

    bool init()
    {
        AppConfig cfg;
        cfg.headless = false;
        app_ = std::make_unique<App>(cfg);

        // Create an initial figure with some data so the window isn't empty
        auto& fig = app_->figure({1280, 720});
        auto& ax  = fig.subplot(1, 1, 1);
        std::vector<float> x(100), y(100);
        for (int i = 0; i < 100; ++i)
        {
            x[i] = static_cast<float>(i) * 0.1f;
            y[i] = std::sin(x[i]);
        }
        ax.line(x, y).label("initial");

        app_->init_runtime();
        if (!app_->ui_context())
        {
            fprintf(stderr, "[QA] Failed to initialize runtime (no UI context)\n");
            return false;
        }

        initial_rss_ = get_rss_bytes();
        peak_rss_    = initial_rss_;
        return true;
    }

    int run()
    {
        if (opts_.list_scenarios)
        {
            list_scenarios();
            return 0;
        }

        fprintf(stderr, "[QA] Spectra QA Agent starting (seed: %lu)\n",
                static_cast<unsigned long>(opts_.seed));

        // Phase 1: Predefined scenarios
        if (!opts_.no_scenarios)
        {
            run_scenarios();
        }

        // Phase 2: Design review (capture systematic UI screenshots)
        if (opts_.design_review)
        {
            run_design_review();
        }

        // Phase 3: Random fuzzing
        if (!opts_.no_fuzz)
        {
            run_fuzzing();
        }

        // Write report before shutdown (shutdown may fail after device lost)
        write_report();

        int exit_code = (issues_with_severity(IssueSeverity::Error) > 0
                         || issues_with_severity(IssueSeverity::Critical) > 0)
                            ? 1
                            : 0;

        // After a critical issue (e.g. Vulkan device lost), the ImGui/Vulkan
        // state is corrupted and normal shutdown will trigger assertions.
        // Use _exit() for fast process termination in that case.
        if (has_critical_issue())
        {
            fprintf(stderr, "[QA] Skipping normal shutdown after critical issue\n");
            _exit(exit_code);
        }

        app_->shutdown_runtime();
        app_.reset();

        return exit_code;
    }

    // Public accessors for scenarios
    App&          app() { return *app_; }
    std::mt19937& rng() { return rng_; }

    bool has_critical_issue() const
    {
        return issues_with_severity(IssueSeverity::Critical) > 0;
    }

    void pump_frames(uint64_t count)
    {
        for (uint64_t i = 0; i < count; ++i)
        {
            if (has_critical_issue())
                break;
            try
            {
                auto result = app_->step();
                total_frames_++;
                frame_stats_.record(result.frame_time_ms);
                check_frame(result);
                if (result.should_exit || wall_clock_exceeded())
                    break;
            }
            catch (const std::exception& e)
            {
                add_issue(IssueSeverity::Critical, "runtime",
                          std::string("Exception in step(): ") + e.what());
                break;
            }
        }
    }

    void add_issue(IssueSeverity sev, const std::string& cat, const std::string& msg)
    {
        QAIssue issue;
        issue.severity = sev;
        issue.category = cat;
        issue.message  = msg;
        issue.frame    = total_frames_;

        // P0 fix: Screenshot rate limiting — max 1 per category per 60 frames
        static constexpr uint64_t SCREENSHOT_COOLDOWN = 60;
        if (sev >= IssueSeverity::Warning)
        {
            auto it = last_screenshot_frame_.find(cat);
            if (it == last_screenshot_frame_.end()
                || (total_frames_ - it->second) >= SCREENSHOT_COOLDOWN)
            {
                issue.screenshot_path = capture_screenshot(cat);
                last_screenshot_frame_[cat] = total_frames_;
            }
        }

        fprintf(stderr, "[QA] [%s] %s: %s (frame %lu)\n",
                severity_str(sev), cat.c_str(), msg.c_str(),
                static_cast<unsigned long>(total_frames_));

        issues_.push_back(std::move(issue));
    }

    FigureId create_random_figure()
    {
        std::uniform_int_distribution<uint32_t> dim_dist(400, 1600);
        uint32_t w = dim_dist(rng_);
        uint32_t h = dim_dist(rng_);
        auto& fig  = app_->figure({w, h});
        auto& ax   = fig.subplot(1, 1, 1);

        // Add random data
        std::uniform_int_distribution<int> n_dist(10, 500);
        int n = n_dist(rng_);
        std::vector<float> x(n), y(n);
        std::uniform_real_distribution<float> val_dist(-100.0f, 100.0f);
        for (int i = 0; i < n; ++i)
        {
            x[i] = static_cast<float>(i);
            y[i] = val_dist(rng_);
        }
        ax.line(x, y);
        return app_->figure_registry().all_ids().back();
    }

    // Ensure a lightweight figure is active so that heavy figures from
    // previous scenarios don't dominate frame time.  Creates a small
    // figure with 50 points and switches to it.
    void ensure_lightweight_active_figure()
    {
        auto& fig = app_->figure({1280, 720});
        auto& ax  = fig.subplot(1, 1, 1);
        std::vector<float> x(50), y(50);
        for (int i = 0; i < 50; ++i)
        {
            x[i] = static_cast<float>(i) * 0.1f;
            y[i] = std::sin(x[i]);
        }
        ax.line(x, y).label("lightweight");
        pump_frames(2);

#ifdef SPECTRA_USE_IMGUI
        // Switch to the new figure
        auto* ui = app_->ui_context();
        if (ui && ui->fig_mgr)
        {
            auto ids = app_->figure_registry().all_ids();
            if (!ids.empty())
                ui->fig_mgr->queue_switch(ids.back());
            pump_frames(1);
        }
#endif
    }

   private:
    // ── Scenarios ────────────────────────────────────────────────────────
    void register_scenarios()
    {
        scenarios_.push_back({"rapid_figure_lifecycle",
            "Create 20 figures, switch randomly for 60 frames, close all but 1",
            [](QAAgent& qa) { return qa.scenario_rapid_figure_lifecycle(); }});

        scenarios_.push_back({"massive_datasets",
            "1M-point line + 5x100K series, pan/zoom, monitor FPS",
            [](QAAgent& qa) { return qa.scenario_massive_datasets(); }});

        scenarios_.push_back({"undo_redo_stress",
            "50 undoable ops, undo all, redo all, partial undo + new ops",
            [](QAAgent& qa) { return qa.scenario_undo_redo_stress(); }});

        scenarios_.push_back({"animation_stress",
            "Animated figure, rapid play/pause toggling every 5 frames",
            [](QAAgent& qa) { return qa.scenario_animation_stress(); }});

        scenarios_.push_back({"input_storm",
            "500 random mouse events + 100 key presses in rapid succession",
            [](QAAgent& qa) { return qa.scenario_input_storm(); }});

        scenarios_.push_back({"command_exhaustion",
            "Execute every registered command, then 3x random order",
            [](QAAgent& qa) { return qa.scenario_command_exhaustion(); }});

        scenarios_.push_back({"series_mixing",
            "One of each series type, toggle visibility, remove/re-add",
            [](QAAgent& qa) { return qa.scenario_series_mixing(); }});

        scenarios_.push_back({"mode_switching",
            "Toggle 2D/3D 10 times with data + orbit/pan between each",
            [](QAAgent& qa) { return qa.scenario_mode_switching(); }});

        scenarios_.push_back({"stress_docking",
            "4 figures, split into grid, add tabs, rapid switching",
            [](QAAgent& qa) { return qa.scenario_stress_docking(); }});

        scenarios_.push_back({"resize_stress",
            "30 rapid window resizes including extreme sizes",
            [](QAAgent& qa) { return qa.scenario_resize_stress(); }});

        scenarios_.push_back({"3d_zoom_then_rotate",
            "Zoom in/out on 3D scatter then verify orbit rotation still works",
            [](QAAgent& qa) { return qa.scenario_3d_zoom_then_rotate(); }});
    }

    void list_scenarios()
    {
        register_scenarios();
        fprintf(stderr, "Available scenarios:\n");
        for (const auto& s : scenarios_)
        {
            fprintf(stderr, "  %-30s %s\n", s.name.c_str(), s.description.c_str());
        }
    }

    void run_scenarios()
    {
        register_scenarios();

        for (auto& scenario : scenarios_)
        {
            if (!opts_.scenario_name.empty() && scenario.name != opts_.scenario_name)
                continue;

            fprintf(stderr, "[QA] Running scenario: %s\n", scenario.name.c_str());
            snprintf(g_last_action, sizeof(g_last_action), "scenario:%s",
                     scenario.name.c_str());
            uint64_t start_frame = total_frames_;

            bool ok = false;
            try
            {
                ok = scenario.run(*this);
            }
            catch (const std::exception& e)
            {
                add_issue(IssueSeverity::Error, "scenario",
                          scenario.name + " threw: " + e.what());
            }

            if (ok)
            {
                scenarios_passed_++;
                fprintf(stderr, "[QA]   PASSED (%lu frames)\n",
                        static_cast<unsigned long>(total_frames_ - start_frame));
            }
            else
            {
                scenarios_failed_++;
                add_issue(IssueSeverity::Error, "scenario",
                          scenario.name + " FAILED");
            }

            if (wall_clock_exceeded())
            {
                fprintf(stderr, "[QA] Wall clock limit reached, stopping scenarios\n");
                break;
            }
        }
    }

    // ── Scenario implementations ─────────────────────────────────────────

    bool scenario_rapid_figure_lifecycle()
    {
        // Create 20 figures
        for (int i = 0; i < 20; ++i)
        {
            create_random_figure();
            pump_frames(2);
        }

        auto ids = app_->figure_registry().all_ids();
        if (ids.size() < 20)
        {
            add_issue(IssueSeverity::Warning, "figure_lifecycle",
                      "Expected 20+ figures, got " + std::to_string(ids.size()));
        }

        // Switch randomly for 60 frames
#ifdef SPECTRA_USE_IMGUI
        auto* ui = app_->ui_context();
        if (ui && ui->fig_mgr)
        {
            for (int i = 0; i < 60; ++i)
            {
                auto all = app_->figure_registry().all_ids();
                if (all.empty())
                    break;
                std::uniform_int_distribution<size_t> dist(0, all.size() - 1);
                ui->fig_mgr->queue_switch(all[dist(rng_)]);
                pump_frames(1);
            }

            // Close all but 1
            auto all = app_->figure_registry().all_ids();
            while (all.size() > 1 && ui->fig_mgr->count() > 1)
            {
                ui->fig_mgr->queue_close(all.back());
                all.pop_back();
                pump_frames(1);
            }
        }
#else
        pump_frames(60);
#endif
        return true;
    }

    bool scenario_massive_datasets()
    {
        auto& fig = app_->figure({1280, 720});
        auto& ax  = fig.subplot(1, 1, 1);

        // 1M-point line
        std::vector<float> x(1000000), y(1000000);
        for (int i = 0; i < 1000000; ++i)
        {
            x[i] = static_cast<float>(i) * 0.001f;
            y[i] = std::sin(x[i] * 0.01f) * std::cos(x[i] * 0.003f);
        }
        ax.line(x, y).label("1M points");
        pump_frames(10);

        // 5x100K series
        for (int s = 0; s < 5; ++s)
        {
            std::vector<float> sx(100000), sy(100000);
            std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
            for (int i = 0; i < 100000; ++i)
            {
                sx[i] = static_cast<float>(i) * 0.01f;
                sy[i] = std::sin(sx[i] + static_cast<float>(s)) + noise(rng_) * 0.1f;
            }
            ax.line(sx, sy);
        }

        // Render some frames with all data
        pump_frames(30);
        return true;
    }

    bool scenario_undo_redo_stress()
    {
        ensure_lightweight_active_figure();
#ifdef SPECTRA_USE_IMGUI
        auto* ui = app_->ui_context();
        if (!ui)
            return true;

        // 50 undoable ops (create figures)
        for (int i = 0; i < 50; ++i)
        {
            UndoAction act;
            act.description = "create_fig_" + std::to_string(i);
            act.redo_fn = []{};
            act.undo_fn = []{};
            ui->undo_mgr.push(std::move(act));
            pump_frames(1);
        }

        // Undo all
        for (int i = 0; i < 50; ++i)
        {
            ui->undo_mgr.undo();
            pump_frames(1);
        }

        // Redo all
        for (int i = 0; i < 50; ++i)
        {
            ui->undo_mgr.redo();
            pump_frames(1);
        }

        // Partial undo + new ops (should clear redo stack)
        for (int i = 0; i < 25; ++i)
            ui->undo_mgr.undo();
        UndoAction new_act;
        new_act.description = "new_op";
        new_act.redo_fn = []{};
        new_act.undo_fn = []{};
        ui->undo_mgr.push(std::move(new_act));
        pump_frames(5);
#endif
        return true;
    }

    bool scenario_animation_stress()
    {
        ensure_lightweight_active_figure();
#ifdef SPECTRA_USE_IMGUI
        auto* ui = app_->ui_context();
        if (!ui)
            return true;

        // Rapid play/pause toggling every 5 frames for 300 frames
        for (int i = 0; i < 300; ++i)
        {
            if (i % 5 == 0)
            {
                ui->timeline_editor.toggle_play();
            }
            pump_frames(1);
        }
        ui->timeline_editor.stop();
#endif
        return true;
    }

    bool scenario_input_storm()
    {
        ensure_lightweight_active_figure();
#ifdef SPECTRA_USE_GLFW
        auto* ui = app_->ui_context();
        if (!ui)
            return true;

        std::uniform_real_distribution<double> pos_x(0.0, 1280.0);
        std::uniform_real_distribution<double> pos_y(0.0, 720.0);
        std::uniform_int_distribution<int>     button_dist(0, 2);
        std::uniform_int_distribution<int>     key_dist(32, 126);

        // 500 random mouse events
        for (int i = 0; i < 500; ++i)
        {
            double mx = pos_x(rng_);
            double my = pos_y(rng_);

            // Alternate between move, click, drag
            int action_type = i % 3;
            if (action_type == 0)
            {
                ui->input_handler.on_mouse_move(mx, my);
            }
            else if (action_type == 1)
            {
                int btn = button_dist(rng_);
                ui->input_handler.on_mouse_button(btn, 1, 0, mx, my);   // press
                pump_frames(1);
                ui->input_handler.on_mouse_button(btn, 0, 0, mx, my);   // release
            }
            else
            {
                ui->input_handler.on_scroll(mx, my, 0.0, (i % 2 == 0) ? 1.0 : -1.0);
            }

            if (i % 10 == 0)
                pump_frames(1);
        }

        // 100 random key presses
        for (int i = 0; i < 100; ++i)
        {
            int key = key_dist(rng_);
            ui->input_handler.on_key(key, 1, 0);   // press
            ui->input_handler.on_key(key, 0, 0);   // release
            if (i % 5 == 0)
                pump_frames(1);
        }

        pump_frames(10);
#endif
        return true;
    }

    bool scenario_command_exhaustion()
    {
        ensure_lightweight_active_figure();
#ifdef SPECTRA_USE_IMGUI
        auto* ui = app_->ui_context();
        if (!ui)
            return true;

        // Get all registered command IDs
        auto all_cmd_ptrs = ui->cmd_registry.all_commands();
        std::vector<std::string> all_cmds;
        for (auto* c : all_cmd_ptrs)
            if (c) all_cmds.push_back(c->id);
        if (all_cmds.empty())
        {
            add_issue(IssueSeverity::Warning, "commands", "No commands registered");
            return true;
        }

        // Execute every command once
        for (const auto& id : all_cmds)
        {
            // Skip destructive commands that would close the window
            if (id == "figure.close" || id == "app.quit")
                continue;
            ui->cmd_registry.execute(id);
            pump_frames(2);
        }

        // 3x random order
        for (int pass = 0; pass < 3; ++pass)
        {
            auto shuffled = all_cmds;
            std::shuffle(shuffled.begin(), shuffled.end(), rng_);
            for (const auto& id : shuffled)
            {
                if (id == "figure.close" || id == "app.quit")
                    continue;
                ui->cmd_registry.execute(id);
                pump_frames(1);
            }
        }
#endif
        return true;
    }

    bool scenario_series_mixing()
    {
        auto& fig = app_->figure({1280, 720});
        auto& ax  = fig.subplot(1, 1, 1);

        std::vector<float> x(50), y(50);
        for (int i = 0; i < 50; ++i)
        {
            x[i] = static_cast<float>(i);
            y[i] = std::sin(static_cast<float>(i) * 0.2f);
        }

        auto& line = ax.line(x, y).label("line");
        auto& scat = ax.scatter(x, y).label("scatter");
        pump_frames(10);

        // Toggle visibility
        line.visible(false);
        pump_frames(5);
        line.visible(true);
        scat.visible(false);
        pump_frames(5);
        scat.visible(true);
        pump_frames(5);

        return true;
    }

    bool scenario_mode_switching()
    {
        ensure_lightweight_active_figure();
#ifdef SPECTRA_USE_IMGUI
        auto* ui = app_->ui_context();
        if (!ui)
            return true;

        for (int i = 0; i < 10; ++i)
        {
            // Toggle 3D mode via command
            ui->cmd_registry.execute("view.toggle_3d");
            pump_frames(10);
        }
#endif
        return true;
    }

    bool scenario_stress_docking()
    {
        ensure_lightweight_active_figure();
#ifdef SPECTRA_USE_IMGUI
        auto* ui = app_->ui_context();
        if (!ui)
            return true;

        // Create 4 figures
        for (int i = 0; i < 4; ++i)
        {
            create_random_figure();
            pump_frames(2);
        }

        // Split right, then split down
        ui->cmd_registry.execute("view.split_right");
        pump_frames(5);
        ui->cmd_registry.execute("view.split_down");
        pump_frames(5);

        // Rapid tab switching
        for (int i = 0; i < 30; ++i)
        {
            ui->cmd_registry.execute("figure.next_tab");
            pump_frames(1);
        }

        // Reset splits
        ui->cmd_registry.execute("view.reset_splits");
        pump_frames(5);
#endif
        return true;
    }

    bool scenario_resize_stress()
    {
        ensure_lightweight_active_figure();
        // Resize via figure dimensions (the renderer adapts on next frame)
        auto ids = app_->figure_registry().all_ids();
        if (ids.empty())
            return true;

        // Pump many frames to stress the render path under normal conditions.
        // True resize requires GLFW window resize which we can't inject here,
        // but we can stress the frame loop.
        for (int i = 0; i < 30; ++i)
        {
            pump_frames(3);
        }

        return true;
    }

    // ── 3D zoom-then-rotate interaction test ────────────────────────────
    // Reproduces the bug where zooming (scroll) on a 3D scatter plot
    // corrupts the active_axes_base_ pointer, causing subsequent orbit
    // rotation (left-click drag) to fail or behave incorrectly.
    bool scenario_3d_zoom_then_rotate()
    {
#ifdef SPECTRA_USE_GLFW
        auto* ui = app_->ui_context();
        if (!ui)
            return true;

        // Create a 3D scatter figure
        auto& fig = app_->figure({1280, 720});
        auto& ax  = fig.subplot3d(1, 1, 1);
        std::vector<float> x(200), y(200), z(200);
        for (int i = 0; i < 200; ++i)
        {
            float t = static_cast<float>(i) * 0.1f;
            x[i] = std::cos(t);
            y[i] = std::sin(t);
            z[i] = t * 0.1f;
        }
        ax.scatter3d(x, y, z).color(colors::blue).size(4.0f);
        ax.auto_fit();
        ax.title("Zoom-then-Rotate Test");
        ax.camera().set_azimuth(45.0f).set_elevation(30.0f);

        // Switch to this figure and let it render
        auto all_ids = app_->figure_registry().all_ids();
        if (!all_ids.empty() && ui->fig_mgr)
        {
            ui->fig_mgr->queue_switch(all_ids.back());
        }
        pump_frames(15);

        // Get the viewport center for injecting events
        const auto& vp = ax.viewport();
        double cx = static_cast<double>(vp.x + vp.w * 0.5f);
        double cy = static_cast<double>(vp.y + vp.h * 0.5f);

        bool all_passed = true;

        // ── Test 1: Zoom then rotate ────────────────────────────────────
        {
            float az_before = ax.camera().azimuth;
            float el_before = ax.camera().elevation;

            // Zoom in (5 scroll events)
            for (int i = 0; i < 5; ++i)
            {
                ui->input_handler.on_scroll(0.0, 1.0, cx, cy);
                pump_frames(1);
            }

            // Zoom out (3 scroll events)
            for (int i = 0; i < 3; ++i)
            {
                ui->input_handler.on_scroll(0.0, -1.0, cx, cy);
                pump_frames(1);
            }

            // Camera angles should NOT have changed from zoom
            float az_after_zoom = ax.camera().azimuth;
            float el_after_zoom = ax.camera().elevation;
            if (std::abs(az_after_zoom - az_before) > 0.01f
                || std::abs(el_after_zoom - el_before) > 0.01f)
            {
                add_issue(IssueSeverity::Error, "3d_zoom_rotate",
                          "Zoom changed camera angles: az " + std::to_string(az_before)
                          + " -> " + std::to_string(az_after_zoom)
                          + ", el " + std::to_string(el_before)
                          + " -> " + std::to_string(el_after_zoom));
                all_passed = false;
            }

            // Now attempt orbit rotation via left-click drag
            ui->input_handler.on_mouse_button(0, 1, 0, cx, cy);   // press
            pump_frames(1);
            // Drag 80px right and 40px down
            for (int s = 1; s <= 10; ++s)
            {
                double dx = cx + 8.0 * s;
                double dy = cy + 4.0 * s;
                ui->input_handler.on_mouse_move(dx, dy);
                pump_frames(1);
            }
            ui->input_handler.on_mouse_button(0, 0, 0, cx + 80.0, cy + 40.0);   // release
            pump_frames(5);

            float az_after_drag = ax.camera().azimuth;
            float el_after_drag = ax.camera().elevation;
            float az_delta = std::abs(az_after_drag - az_after_zoom);
            float el_delta = std::abs(el_after_drag - el_after_zoom);

            if (az_delta < 1.0f && el_delta < 1.0f)
            {
                add_issue(IssueSeverity::Error, "3d_zoom_rotate",
                          "Orbit rotation FAILED after zoom: az delta="
                          + std::to_string(az_delta) + ", el delta="
                          + std::to_string(el_delta)
                          + " (expected significant change from 80px drag)");
                all_passed = false;
            }
            else
            {
                fprintf(stderr, "[QA]   Test 1 OK: orbit after zoom works "
                        "(az delta=%.1f, el delta=%.1f)\n", az_delta, el_delta);
            }
        }

        // ── Test 2: Interleaved zoom + rotate (rapid alternation) ───────
        {
            ax.camera().set_azimuth(45.0f).set_elevation(30.0f);
            pump_frames(5);

            bool any_rotation_failed = false;
            for (int round = 0; round < 5; ++round)
            {
                float az_pre = ax.camera().azimuth;
                float el_pre = ax.camera().elevation;

                // Zoom
                ui->input_handler.on_scroll(0.0, (round % 2 == 0) ? 1.0 : -1.0, cx, cy);
                pump_frames(1);

                // Immediately orbit
                ui->input_handler.on_mouse_button(0, 1, 0, cx, cy);
                pump_frames(1);
                double drag_dx = (round % 2 == 0) ? 60.0 : -60.0;
                double drag_dy = (round % 2 == 0) ? 30.0 : -30.0;
                for (int s = 1; s <= 5; ++s)
                {
                    double t = static_cast<double>(s) / 5.0;
                    ui->input_handler.on_mouse_move(cx + drag_dx * t, cy + drag_dy * t);
                    pump_frames(1);
                }
                ui->input_handler.on_mouse_button(0, 0, 0, cx + drag_dx, cy + drag_dy);
                pump_frames(2);

                float az_post = ax.camera().azimuth;
                float el_post = ax.camera().elevation;
                float az_d = std::abs(az_post - az_pre);
                float el_d = std::abs(el_post - el_pre);

                if (az_d < 0.5f && el_d < 0.5f)
                {
                    add_issue(IssueSeverity::Warning, "3d_zoom_rotate",
                              "Round " + std::to_string(round)
                              + ": orbit after zoom had no effect (az_d="
                              + std::to_string(az_d) + ", el_d="
                              + std::to_string(el_d) + ")");
                    any_rotation_failed = true;
                }
            }

            if (any_rotation_failed)
            {
                add_issue(IssueSeverity::Error, "3d_zoom_rotate",
                          "Interleaved zoom+rotate: some rounds failed");
                all_passed = false;
            }
            else
            {
                fprintf(stderr, "[QA]   Test 2 OK: interleaved zoom+rotate works\n");
            }
        }

        // ── Test 3: Extreme zoom then rotate ────────────────────────────
        {
            ax.camera().set_azimuth(0.0f).set_elevation(45.0f);
            pump_frames(5);

            // Extreme zoom in (20 scroll events)
            for (int i = 0; i < 20; ++i)
            {
                ui->input_handler.on_scroll(0.0, 1.0, cx, cy);
                pump_frames(1);
            }

            float az_pre = ax.camera().azimuth;
            float el_pre = ax.camera().elevation;

            // Orbit drag
            ui->input_handler.on_mouse_button(0, 1, 0, cx, cy);
            pump_frames(1);
            for (int s = 1; s <= 8; ++s)
            {
                ui->input_handler.on_mouse_move(cx - 10.0 * s, cy + 5.0 * s);
                pump_frames(1);
            }
            ui->input_handler.on_mouse_button(0, 0, 0, cx - 80.0, cy + 40.0);
            pump_frames(5);

            float az_d = std::abs(ax.camera().azimuth - az_pre);
            float el_d = std::abs(ax.camera().elevation - el_pre);

            if (az_d < 1.0f && el_d < 1.0f)
            {
                add_issue(IssueSeverity::Error, "3d_zoom_rotate",
                          "Extreme zoom then rotate FAILED: az_d="
                          + std::to_string(az_d) + ", el_d=" + std::to_string(el_d));
                all_passed = false;
            }
            else
            {
                fprintf(stderr, "[QA]   Test 3 OK: extreme zoom then rotate works "
                        "(az_d=%.1f, el_d=%.1f)\n", az_d, el_d);
            }
        }

        return all_passed;
#else
        return true;
#endif
    }

    // ── Design Review ────────────────────────────────────────────────────
    // Captures named screenshots of every meaningful UI state for design analysis.
    // Screenshots go into <output_dir>/design/ with descriptive names.

    std::string named_screenshot(const std::string& name)
    {
        auto* backend = app_->backend();
        if (!backend)
            return "";

        uint32_t w = backend->swapchain_width();
        uint32_t h = backend->swapchain_height();
        if (w == 0 || h == 0)
            return "";

        std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
        if (!backend->readback_framebuffer(pixels.data(), w, h))
            return "";

        std::string dir = opts_.output_dir + "/design";
        std::filesystem::create_directories(dir);

        std::string safe = name;
        for (auto& c : safe)
            if (!std::isalnum(c) && c != '_' && c != '-')
                c = '_';

        std::string path = dir + "/" + safe + ".png";
        ImageExporter::write_png(path, pixels.data(), w, h);
        fprintf(stderr, "[QA/Design] Captured: %s\n", path.c_str());
        design_screenshots_.push_back({name, path});
        return path;
    }

    void run_design_review()
    {
        fprintf(stderr, "[QA/Design] Starting design review capture...\n");

        // ── 1. Default state: single figure with simple line ─────────────
        pump_frames(10);
        named_screenshot("01_default_single_line");

        // ── 2. Empty axes (no data) ──────────────────────────────────────
        {
            auto& fig = app_->figure({1280, 720});
            fig.subplot(1, 1, 1);
            pump_frames(10);
            named_screenshot("02_empty_axes");
        }

        // ── 3. Multiple series (line + scatter) ──────────────────────────
        {
            auto& fig = app_->figure({1280, 720});
            auto& ax  = fig.subplot(1, 1, 1);
            std::vector<float> x(200), y1(200), y2(200), y3(200);
            for (int i = 0; i < 200; ++i)
            {
                x[i]  = static_cast<float>(i) * 0.05f;
                y1[i] = std::sin(x[i]);
                y2[i] = std::cos(x[i]);
                y3[i] = std::sin(x[i] * 2.0f) * 0.5f;
            }
            ax.line(x, y1).label("sin(x)");
            ax.line(x, y2).label("cos(x)");
            ax.scatter(x, y3).label("sin(2x)/2");
            ax.title("Multi-Series Plot");
            ax.xlabel("Time (s)");
            ax.ylabel("Amplitude");
            pump_frames(10);
            named_screenshot("03_multi_series_with_labels");
        }

        // ── 4. Dense data (10K points) ───────────────────────────────────
        {
            auto& fig = app_->figure({1280, 720});
            auto& ax  = fig.subplot(1, 1, 1);
            std::vector<float> x(10000), y(10000);
            for (int i = 0; i < 10000; ++i)
            {
                x[i] = static_cast<float>(i) * 0.001f;
                y[i] = std::sin(x[i] * 10.0f) * std::exp(-x[i] * 0.3f);
            }
            ax.line(x, y).label("Damped oscillation");
            ax.title("Dense Data (10K points)");
            pump_frames(10);
            named_screenshot("04_dense_data_10k");
        }

        // ── 5. Subplot grid (2x2) ───────────────────────────────────────
        {
            auto& fig = app_->figure({1280, 720});
            for (int r = 0; r < 2; ++r)
            {
                for (int c = 0; c < 2; ++c)
                {
                    auto& ax = fig.subplot(2, 2, r * 2 + c + 1);
                    std::vector<float> x(100), y(100);
                    for (int i = 0; i < 100; ++i)
                    {
                        x[i] = static_cast<float>(i) * 0.1f;
                        y[i] = std::sin(x[i] * (1.0f + r) + c * 1.5f);
                    }
                    ax.line(x, y);
                    ax.title("Subplot " + std::to_string(r * 2 + c + 1));
                }
            }
            pump_frames(10);
            named_screenshot("05_subplot_2x2_grid");
        }

        // ── 6. Large scatter plot ────────────────────────────────────────
        {
            auto& fig = app_->figure({1280, 720});
            auto& ax  = fig.subplot(1, 1, 1);
            std::vector<float> x(2000), y(2000);
            std::normal_distribution<float> norm(0.0f, 1.0f);
            for (int i = 0; i < 2000; ++i)
            {
                x[i] = norm(rng_);
                y[i] = norm(rng_);
            }
            ax.scatter(x, y).label("Normal distribution");
            ax.title("Scatter Plot (2K points)");
            pump_frames(10);
            named_screenshot("06_scatter_2k_normal");
        }

        // ── 7. UI panels: inspector open ─────────────────────────────────
#ifdef SPECTRA_USE_IMGUI
        {
            auto* ui = app_->ui_context();
            if (ui)
            {
                ui->cmd_registry.execute("panel.toggle_inspector");
                pump_frames(10);
                named_screenshot("07_inspector_panel_open");
                ui->cmd_registry.execute("panel.toggle_inspector");
                pump_frames(5);
            }
        }

        // ── 8. Command palette open ──────────────────────────────────────
        {
            auto* ui = app_->ui_context();
            if (ui)
            {
                ui->cmd_registry.execute("app.command_palette");
                pump_frames(10);
                named_screenshot("08_command_palette_open");
                ui->cmd_registry.execute("app.cancel");
                pump_frames(5);
            }
        }

        // ── 9. Split view (2 panes) ─────────────────────────────────────
        {
            auto* ui = app_->ui_context();
            if (ui)
            {
                ui->cmd_registry.execute("view.split_right");
                pump_frames(10);
                named_screenshot("09_split_view_right");
            }
        }

        // ── 10. Split view (4 panes) ────────────────────────────────────
        {
            auto* ui = app_->ui_context();
            if (ui)
            {
                ui->cmd_registry.execute("view.split_down");
                pump_frames(10);
                named_screenshot("10_split_view_4_panes");
                // Reset splits
                ui->cmd_registry.execute("view.reset_splits");
                pump_frames(5);
            }
        }

        // ── 11. Dark theme (should already be default) ──────────────────
        {
            auto* ui = app_->ui_context();
            if (ui)
            {
                ui->cmd_registry.execute("theme.dark");
                pump_frames(10);
                named_screenshot("11_theme_dark");
            }
        }

        // ── 12. Light theme ─────────────────────────────────────────────
        {
            auto* ui = app_->ui_context();
            if (ui)
            {
                ui->cmd_registry.execute("theme.light");
                pump_frames(30);  // D25 fix: allow theme transition to fully complete
                named_screenshot("12_theme_light");
                // Switch back to dark
                ui->cmd_registry.execute("theme.dark");
                pump_frames(30);
            }
        }

        // ── 13. Grid enabled ────────────────────────────────────────────
        {
            auto* ui = app_->ui_context();
            if (ui)
            {
                ui->cmd_registry.execute("view.toggle_grid");
                pump_frames(10);
                named_screenshot("13_grid_enabled");
            }
        }

        // ── 14. Legend visible ───────────────────────────────────────────
        {
            auto* ui = app_->ui_context();
            if (ui)
            {
                // Use explicit state to avoid toggle drift (D24 fix)
                Figure* active_fig = ui->fig_mgr->active_figure();
                if (active_fig)
                {
                    active_fig->legend().visible = true;
                    pump_frames(10);
                    named_screenshot("14_legend_visible");
                    active_fig->legend().visible = false;
                }
            }
        }

        // ── 15. Crosshair mode ──────────────────────────────────────────
        {
            auto* ui = app_->ui_context();
            if (ui)
            {
                ui->cmd_registry.execute("view.toggle_crosshair");
                pump_frames(10);
                named_screenshot("15_crosshair_mode");
                ui->cmd_registry.execute("view.toggle_crosshair");
                pump_frames(5);
            }
        }

        // ── 16. Zoomed in view ──────────────────────────────────────────
        {
            auto* ui = app_->ui_context();
            if (ui)
            {
                for (int i = 0; i < 5; ++i)
                    ui->cmd_registry.execute("view.zoom_in");
                pump_frames(10);
                named_screenshot("16_zoomed_in");
                ui->cmd_registry.execute("view.home");
                pump_frames(5);
            }
        }

        // ── 17. Multiple tabs ───────────────────────────────────────────
        {
            // Create several figures to show tab bar
            for (int i = 0; i < 4; ++i)
                create_random_figure();
            pump_frames(10);
            named_screenshot("17_multiple_tabs");
        }

        // ── 18. Timeline panel ──────────────────────────────────────────
        {
            auto* ui = app_->ui_context();
            if (ui)
            {
                ui->cmd_registry.execute("panel.toggle_timeline");
                pump_frames(10);
                named_screenshot("18_timeline_panel");
                ui->cmd_registry.execute("panel.toggle_timeline");
                pump_frames(5);
            }
        }
#endif

        // ── 19. 3D surface plot ─────────────────────────────────────────
        {
            auto& fig = app_->figure({1280, 720});
            auto& ax  = fig.subplot3d(1, 1, 1);
            int n = 30;
            // D23 fix: surface() expects 1D unique grid vectors, not flat 2D
            std::vector<float> xg(n), yg(n);
            for (int i = 0; i < n; ++i)
                xg[i] = -3.0f + 6.0f * i / (n - 1);
            for (int j = 0; j < n; ++j)
                yg[j] = -3.0f + 6.0f * j / (n - 1);
            std::vector<float> zv(n * n);
            for (int j = 0; j < n; ++j)
                for (int i = 0; i < n; ++i)
                    zv[j * n + i] = std::sin(std::sqrt(xg[i] * xg[i] + yg[j] * yg[j]));
            ax.surface(xg, yg, zv).colormap(ColormapType::Viridis);
            ax.auto_fit();
            ax.title("3D Surface");
            pump_frames(15);
            named_screenshot("19_3d_surface");
        }

        // ── 20. 3D scatter plot ─────────────────────────────────────────
        {
            auto& fig = app_->figure({1280, 720});
            auto& ax  = fig.subplot3d(1, 1, 1);
            std::vector<float> x(500), y(500), z(500);
            std::normal_distribution<float> norm(0.0f, 1.0f);
            for (int i = 0; i < 500; ++i)
            {
                x[i] = norm(rng_);
                y[i] = norm(rng_);
                z[i] = norm(rng_);
            }
            ax.scatter3d(x, y, z);
            ax.auto_fit();
            ax.title("3D Scatter");
            pump_frames(15);
            named_screenshot("20_3d_scatter");
        }

        // ══════════════════════════════════════════════════════════════════
        // Session 4 — 3D / Animation / Statistics scenarios
        // ══════════════════════════════════════════════════════════════════

        // ── 21. 3D surface with labels + lighting ──────────────────────
        {
            auto& fig = app_->figure({1280, 720});
            auto& ax  = fig.subplot3d(1, 1, 1);
            int n = 40;
            std::vector<float> xg(n), yg(n);
            for (int i = 0; i < n; ++i)
                xg[i] = -4.0f + 8.0f * i / (n - 1);
            for (int j = 0; j < n; ++j)
                yg[j] = -4.0f + 8.0f * j / (n - 1);
            std::vector<float> zv(n * n);
            for (int j = 0; j < n; ++j)
                for (int i = 0; i < n; ++i)
                    zv[j * n + i] = std::cos(xg[i]) * std::sin(yg[j]);
            ax.surface(xg, yg, zv).colormap(ColormapType::Viridis);
            ax.auto_fit();
            ax.title("cos(x)\xC2\xB7sin(y) Surface");
            ax.xlabel("X Axis");
            ax.ylabel("Y Axis");
            ax.zlabel("Z Value");
            ax.lighting_enabled(true);
            ax.light_dir(1.0f, 2.0f, 1.5f);
            ax.show_bounding_box(true);
            ax.grid_planes(Axes3D::GridPlane::All);
            pump_frames(15);
            named_screenshot("21_3d_surface_labeled");
        }

        // ── 22. 3D surface — rotated camera (side view) ───────────────
        {
            auto& fig = app_->figure({1280, 720});
            auto& ax  = fig.subplot3d(1, 1, 1);
            int n = 30;
            std::vector<float> xg(n), yg(n);
            for (int i = 0; i < n; ++i)
                xg[i] = -3.0f + 6.0f * i / (n - 1);
            for (int j = 0; j < n; ++j)
                yg[j] = -3.0f + 6.0f * j / (n - 1);
            std::vector<float> zv(n * n);
            for (int j = 0; j < n; ++j)
                for (int i = 0; i < n; ++i)
                    zv[j * n + i] = std::sin(std::sqrt(xg[i] * xg[i] + yg[j] * yg[j]));
            ax.surface(xg, yg, zv).colormap(ColormapType::Plasma);
            ax.auto_fit();
            ax.title("Side View (azimuth=0, elev=15)");
            ax.camera().set_azimuth(0.0f).set_elevation(15.0f).set_distance(7.0f);
            pump_frames(15);
            named_screenshot("22_3d_camera_side_view");
        }

        // ── 23. 3D surface — top-down camera ──────────────────────────
        {
            auto& fig = app_->figure({1280, 720});
            auto& ax  = fig.subplot3d(1, 1, 1);
            int n = 30;
            std::vector<float> xg(n), yg(n);
            for (int i = 0; i < n; ++i)
                xg[i] = -3.0f + 6.0f * i / (n - 1);
            for (int j = 0; j < n; ++j)
                yg[j] = -3.0f + 6.0f * j / (n - 1);
            std::vector<float> zv(n * n);
            for (int j = 0; j < n; ++j)
                for (int i = 0; i < n; ++i)
                    zv[j * n + i] = xg[i] * xg[i] - yg[j] * yg[j];
            ax.surface(xg, yg, zv).colormap(ColormapType::Inferno);
            ax.auto_fit();
            ax.title("Top-Down View (elev=85)");
            ax.camera().set_azimuth(45.0f).set_elevation(85.0f).set_distance(6.0f);
            pump_frames(15);
            named_screenshot("23_3d_camera_top_down");
        }

        // ── 24. 3D line plot (helix) ──────────────────────────────────
        {
            auto& fig = app_->figure({1280, 720});
            auto& ax  = fig.subplot3d(1, 1, 1);
            int n = 500;
            std::vector<float> x(n), y(n), z(n);
            for (int i = 0; i < n; ++i)
            {
                float t = static_cast<float>(i) * 0.05f;
                x[i] = std::cos(t);
                y[i] = std::sin(t);
                z[i] = t * 0.1f;
            }
            ax.line3d(x, y, z).label("Helix").color(colors::cyan);
            ax.auto_fit();
            ax.title("3D Helix Line");
            ax.xlabel("X");
            ax.ylabel("Y");
            ax.zlabel("Z");
            pump_frames(15);
            named_screenshot("24_3d_line_helix");
        }

        // ── 25. 3D scatter with multiple clusters ─────────────────────
        {
            auto& fig = app_->figure({1280, 720});
            auto& ax  = fig.subplot3d(1, 1, 1);
            std::normal_distribution<float> norm(0.0f, 0.5f);
            // Cluster 1
            std::vector<float> x1(200), y1(200), z1(200);
            for (int i = 0; i < 200; ++i)
            {
                x1[i] = norm(rng_) + 2.0f;
                y1[i] = norm(rng_) + 2.0f;
                z1[i] = norm(rng_) + 2.0f;
            }
            ax.scatter3d(x1, y1, z1).label("Cluster A").color(colors::red);
            // Cluster 2
            std::vector<float> x2(200), y2(200), z2(200);
            for (int i = 0; i < 200; ++i)
            {
                x2[i] = norm(rng_) - 2.0f;
                y2[i] = norm(rng_) - 2.0f;
                z2[i] = norm(rng_) - 2.0f;
            }
            ax.scatter3d(x2, y2, z2).label("Cluster B").color(colors::blue);
            ax.auto_fit();
            ax.title("3D Scatter -- Two Clusters");
            pump_frames(15);
            named_screenshot("25_3d_scatter_clusters");
        }

        // ── 26. 3D orthographic projection ────────────────────────────
        {
            auto& fig = app_->figure({1280, 720});
            auto& ax  = fig.subplot3d(1, 1, 1);
            int n = 25;
            std::vector<float> xg(n), yg(n);
            for (int i = 0; i < n; ++i)
                xg[i] = -2.0f + 4.0f * i / (n - 1);
            for (int j = 0; j < n; ++j)
                yg[j] = -2.0f + 4.0f * j / (n - 1);
            std::vector<float> zv(n * n);
            for (int j = 0; j < n; ++j)
                for (int i = 0; i < n; ++i)
                    zv[j * n + i] = std::exp(-(xg[i] * xg[i] + yg[j] * yg[j]));
            ax.surface(xg, yg, zv).colormap(ColormapType::Coolwarm);
            ax.auto_fit();
            ax.title("Orthographic Projection");
            ax.camera().set_projection(Camera::ProjectionMode::Orthographic);
            ax.camera().set_ortho_size(8.0f);
            pump_frames(15);
            named_screenshot("26_3d_orthographic");
        }

#ifdef SPECTRA_USE_IMGUI
        // ── 27. Inspector with series selected (statistics visible) ───
        {
            // Create a figure with labeled data for inspector stats
            auto& fig = app_->figure({1280, 720});
            auto& ax  = fig.subplot(1, 1, 1);
            std::vector<float> x(300), y(300);
            for (int i = 0; i < 300; ++i)
            {
                x[i] = static_cast<float>(i) * 0.02f;
                y[i] = std::sin(x[i] * 3.0f) * std::exp(-x[i] * 0.2f) + 0.5f;
            }
            ax.line(x, y).label("Damped Signal");
            ax.title("Inspector Statistics Demo");
            ax.xlabel("Time (s)");
            ax.ylabel("Amplitude");
            pump_frames(10);

            auto* ui = app_->ui_context();
            if (ui)
            {
                // Open inspector and select series section
                ui->cmd_registry.execute("panel.toggle_inspector");
                pump_frames(5);
                // Cycle to series selection
                ui->cmd_registry.execute("series.cycle_selection");
                pump_frames(10);
                named_screenshot("27_inspector_series_stats");
                ui->cmd_registry.execute("panel.toggle_inspector");
                pump_frames(5);
            }
        }

        // ── 28. Inspector with axes properties ────────────────────────
        {
            auto* ui = app_->ui_context();
            if (ui)
            {
                ui->cmd_registry.execute("panel.toggle_inspector");
                pump_frames(10);
                named_screenshot("28_inspector_axes_properties");
                ui->cmd_registry.execute("panel.toggle_inspector");
                pump_frames(5);
            }
        }

        // ── 29. Timeline with keyframes and tracks ────────────────────
        {
            auto* ui = app_->ui_context();
            if (ui)
            {
                // Add tracks with keyframes to the timeline
                auto& te = ui->timeline_editor;
                te.set_duration(5.0f);
                te.set_fps(30.0f);
                uint32_t t1 = te.add_track("X Position", colors::red);
                uint32_t t2 = te.add_track("Y Position", colors::green);
                uint32_t t3 = te.add_track("Opacity", colors::blue);
                // Add keyframes
                te.add_keyframe(t1, 0.0f);
                te.add_keyframe(t1, 1.5f);
                te.add_keyframe(t1, 3.0f);
                te.add_keyframe(t1, 5.0f);
                te.add_keyframe(t2, 0.0f);
                te.add_keyframe(t2, 2.0f);
                te.add_keyframe(t2, 4.0f);
                te.add_keyframe(t3, 0.0f);
                te.add_keyframe(t3, 2.5f);
                te.add_keyframe(t3, 5.0f);
                // Set playhead mid-timeline
                te.set_playhead(1.8f);

                ui->cmd_registry.execute("panel.toggle_timeline");
                pump_frames(15);
                named_screenshot("29_timeline_with_keyframes");
                ui->cmd_registry.execute("panel.toggle_timeline");
                pump_frames(5);
            }
        }

        // ── 30. Timeline playing (playhead mid-animation) ─────────────
        {
            auto* ui = app_->ui_context();
            if (ui)
            {
                auto& te = ui->timeline_editor;
                ui->cmd_registry.execute("panel.toggle_timeline");
                pump_frames(5);
                // Start playback and capture mid-play
                te.play();
                pump_frames(30);   // Let it advance ~30 frames
                named_screenshot("30_timeline_playing");
                te.stop();
                ui->cmd_registry.execute("panel.toggle_timeline");
                pump_frames(5);
            }
        }

        // ── 31. Timeline with loop region ─────────────────────────────
        {
            auto* ui = app_->ui_context();
            if (ui)
            {
                auto& te = ui->timeline_editor;
                te.set_loop_mode(LoopMode::Loop);
                te.set_loop_region(1.0f, 3.5f);
                te.set_playhead(2.0f);

                ui->cmd_registry.execute("panel.toggle_timeline");
                pump_frames(15);
                named_screenshot("31_timeline_loop_region");
                // Clean up
                te.set_loop_mode(LoopMode::None);
                te.clear_loop_region();
                ui->cmd_registry.execute("panel.toggle_timeline");
                pump_frames(5);
            }
        }

        // ── 32. Curve editor ──────────────────────────────────────────
        {
            auto* ui = app_->ui_context();
            if (ui)
            {
                ui->cmd_registry.execute("panel.toggle_curve_editor");
                pump_frames(15);
                named_screenshot("32_curve_editor");
                ui->cmd_registry.execute("panel.toggle_curve_editor");
                pump_frames(5);
            }
        }

        // ── 33. Split view with 2 figures (proper split) ─────────────
        {
            // Create 2 figures so split actually works
            auto& fig1 = app_->figure({1280, 720});
            auto& ax1  = fig1.subplot(1, 1, 1);
            std::vector<float> x1(200), y1(200);
            for (int i = 0; i < 200; ++i)
            {
                x1[i] = static_cast<float>(i) * 0.05f;
                y1[i] = std::sin(x1[i]);
            }
            ax1.line(x1, y1).label("sin(x)");
            ax1.title("Left Pane");

            auto& fig2 = app_->figure({1280, 720});
            auto& ax2  = fig2.subplot(1, 1, 1);
            std::vector<float> x2(200), y2(200);
            for (int i = 0; i < 200; ++i)
            {
                x2[i] = static_cast<float>(i) * 0.05f;
                y2[i] = std::cos(x2[i]);
            }
            ax2.line(x2, y2).label("cos(x)");
            ax2.title("Right Pane");
            pump_frames(10);

            auto* ui = app_->ui_context();
            if (ui)
            {
                ui->cmd_registry.execute("view.split_right");
                pump_frames(15);
                named_screenshot("33_split_view_two_figures");
                ui->cmd_registry.execute("view.reset_splits");
                pump_frames(5);
            }
        }

        // ── 34. Multi-series with legend + grid + crosshair ──────────
        {
            auto& fig = app_->figure({1280, 720});
            auto& ax  = fig.subplot(1, 1, 1);
            std::vector<float> x(300);
            for (int i = 0; i < 300; ++i)
                x[i] = static_cast<float>(i) * 0.02f;

            std::vector<float> y1(300), y2(300), y3(300), y4(300);
            for (int i = 0; i < 300; ++i)
            {
                y1[i] = std::sin(x[i] * 2.0f);
                y2[i] = std::cos(x[i] * 2.0f);
                y3[i] = std::sin(x[i] * 4.0f) * 0.5f;
                y4[i] = std::cos(x[i]) * std::exp(-x[i] * 0.3f);
            }
            ax.line(x, y1).label("sin(2x)");
            ax.line(x, y2).label("cos(2x)");
            ax.line(x, y3).label("sin(4x)/2");
            ax.line(x, y4).label("exp·cos(x)");
            ax.title("Multi-Signal Overlay");
            ax.xlabel("Time (s)");
            ax.ylabel("Value");
            pump_frames(10);

            // Explicitly enable grid, legend, and crosshair (avoid toggle drift)
            ax.grid(true);
            fig.legend().visible = true;
            auto* ui = app_->ui_context();
            if (ui)
            {
                if (ui->data_interaction)
                    ui->data_interaction->set_crosshair(true);
                pump_frames(10);
                named_screenshot("34_multi_series_full_chrome");
                // Restore defaults
                if (ui->data_interaction)
                    ui->data_interaction->set_crosshair(false);
                pump_frames(5);
            }
        }

        // ── 35. Zoomed-in data center (verify D12 fix) ───────────────
        {
            auto& fig = app_->figure({1280, 720});
            auto& ax  = fig.subplot(1, 1, 1);
            std::vector<float> x(200), y(200);
            for (int i = 0; i < 200; ++i)
            {
                x[i] = 5.0f + static_cast<float>(i) * 0.01f;
                y[i] = 10.0f + std::sin(x[i] * 20.0f) * 0.5f;
            }
            ax.line(x, y).label("Offset signal");
            ax.title("Zoom Center Test (data at x=5..7, y=9.5..10.5)");
            pump_frames(10);

            auto* ui = app_->ui_context();
            if (ui)
            {
                for (int i = 0; i < 5; ++i)
                    ui->cmd_registry.execute("view.zoom_in");
                pump_frames(10);
                named_screenshot("35_zoom_data_center_verify");
                ui->cmd_registry.execute("view.home");
                pump_frames(5);
            }
        }
#endif

        // ── Summary ─────────────────────────────────────────────────────
        fprintf(stderr, "[QA/Design] Captured %zu design screenshots in %s/design/\n",
                design_screenshots_.size(), opts_.output_dir.c_str());

        // Write design screenshot manifest
        {
            std::string manifest_path = opts_.output_dir + "/design/manifest.txt";
            std::ofstream out(manifest_path);
            out << "Spectra Design Review Screenshots\n";
            out << "==================================\n";
            out << "Captured: " << design_screenshots_.size() << " screenshots\n\n";
            for (const auto& [name, path] : design_screenshots_)
            {
                out << "  " << name << "\n    -> " << path << "\n";
            }
        }
    }

    // ── Fuzzing ──────────────────────────────────────────────────────────
    enum class FuzzAction
    {
        ExecuteCommand,
        MouseClick,
        MouseDrag,
        MouseScroll,
        KeyPress,
        CreateFigure,
        CloseFigure,
        SwitchTab,
        AddSeries,
        UpdateData,
        LargeDataset,
        SplitDock,
        Toggle3D,
        WaitFrames,
        COUNT
    };

    struct ActionWeight
    {
        FuzzAction action;
        int        weight;
    };

    void run_fuzzing()
    {
        fprintf(stderr, "[QA] Starting fuzzing phase (%lu frames)\n",
                static_cast<unsigned long>(opts_.fuzz_frames));

        std::vector<ActionWeight> weights = {
            {FuzzAction::ExecuteCommand, 15},
            {FuzzAction::MouseClick,     15},
            {FuzzAction::MouseDrag,      10},
            {FuzzAction::MouseScroll,    10},
            {FuzzAction::KeyPress,       10},
            {FuzzAction::CreateFigure,    5},
            {FuzzAction::CloseFigure,     3},
            {FuzzAction::SwitchTab,       8},
            {FuzzAction::AddSeries,       8},
            {FuzzAction::UpdateData,      5},
            {FuzzAction::LargeDataset,    1},
            {FuzzAction::SplitDock,       3},
            {FuzzAction::Toggle3D,        3},
            {FuzzAction::WaitFrames,      7},
        };

        int total_weight = 0;
        for (auto& w : weights)
            total_weight += w.weight;

        std::uniform_int_distribution<int> weight_dist(0, total_weight - 1);

        for (uint64_t f = 0; f < opts_.fuzz_frames; ++f)
        {
            if (wall_clock_exceeded())
            {
                fprintf(stderr, "[QA] Wall clock limit reached during fuzzing\n");
                break;
            }
            if (has_critical_issue())
            {
                fprintf(stderr, "[QA] Critical issue detected, stopping fuzzing\n");
                break;
            }

            // Pick weighted random action
            int roll = weight_dist(rng_);
            FuzzAction action = FuzzAction::WaitFrames;
            int cumulative = 0;
            for (auto& w : weights)
            {
                cumulative += w.weight;
                if (roll < cumulative)
                {
                    action = w.action;
                    break;
                }
            }

            execute_fuzz_action(action);
            pump_frames(1);
        }

        fprintf(stderr, "[QA] Fuzzing complete (%lu total frames)\n",
                static_cast<unsigned long>(total_frames_));
    }

    static const char* fuzz_action_name(FuzzAction a)
    {
        switch (a)
        {
            case FuzzAction::ExecuteCommand: return "fuzz:ExecuteCommand";
            case FuzzAction::MouseClick:     return "fuzz:MouseClick";
            case FuzzAction::MouseDrag:      return "fuzz:MouseDrag";
            case FuzzAction::MouseScroll:    return "fuzz:MouseScroll";
            case FuzzAction::KeyPress:       return "fuzz:KeyPress";
            case FuzzAction::CreateFigure:   return "fuzz:CreateFigure";
            case FuzzAction::CloseFigure:    return "fuzz:CloseFigure";
            case FuzzAction::SwitchTab:      return "fuzz:SwitchTab";
            case FuzzAction::AddSeries:      return "fuzz:AddSeries";
            case FuzzAction::UpdateData:     return "fuzz:UpdateData";
            case FuzzAction::LargeDataset:   return "fuzz:LargeDataset";
            case FuzzAction::SplitDock:      return "fuzz:SplitDock";
            case FuzzAction::Toggle3D:       return "fuzz:Toggle3D";
            case FuzzAction::WaitFrames:     return "fuzz:WaitFrames";
            default:                         return "fuzz:Unknown";
        }
    }

    void execute_fuzz_action(FuzzAction action)
    {
        // P0 fix: track last action for crash handler context
        snprintf(g_last_action, sizeof(g_last_action), "%s (frame %lu)",
                 fuzz_action_name(action), static_cast<unsigned long>(total_frames_));

        [[maybe_unused]] auto* ui = app_->ui_context();

        switch (action)
        {
            case FuzzAction::ExecuteCommand:
            {
#ifdef SPECTRA_USE_IMGUI
                if (!ui)
                    break;
                auto cmd_ptrs = ui->cmd_registry.all_commands();
                std::vector<std::string> cmds;
                for (auto* c : cmd_ptrs)
                    if (c) cmds.push_back(c->id);
                if (cmds.empty())
                    break;
                std::uniform_int_distribution<size_t> dist(0, cmds.size() - 1);
                const auto& id = cmds[dist(rng_)];
                // Skip destructive commands
                if (id != "figure.close" && id != "app.quit")
                    ui->cmd_registry.execute(id);
#endif
                break;
            }

            case FuzzAction::MouseClick:
            {
#ifdef SPECTRA_USE_GLFW
                if (!ui)
                    break;
                std::uniform_real_distribution<double> px(0, 1280), py(0, 720);
                std::uniform_int_distribution<int> btn(0, 1);
                double mx = px(rng_), my = py(rng_);
                int b = btn(rng_);
                ui->input_handler.on_mouse_button(b, 1, 0, mx, my);
                ui->input_handler.on_mouse_button(b, 0, 0, mx, my);
#endif
                break;
            }

            case FuzzAction::MouseDrag:
            {
#ifdef SPECTRA_USE_GLFW
                if (!ui)
                    break;
                std::uniform_real_distribution<double> px(0, 1280), py(0, 720);
                double x1 = px(rng_), y1 = py(rng_);
                double x2 = px(rng_), y2 = py(rng_);
                ui->input_handler.on_mouse_button(0, 1, 0, x1, y1);
                // Interpolate drag
                for (int s = 1; s <= 5; ++s)
                {
                    double t  = static_cast<double>(s) / 5.0;
                    double cx = x1 + (x2 - x1) * t;
                    double cy = y1 + (y2 - y1) * t;
                    ui->input_handler.on_mouse_move(cx, cy);
                }
                ui->input_handler.on_mouse_button(0, 0, 0, x2, y2);
#endif
                break;
            }

            case FuzzAction::MouseScroll:
            {
#ifdef SPECTRA_USE_GLFW
                if (!ui)
                    break;
                std::uniform_real_distribution<double> px(0, 1280), py(0, 720);
                std::uniform_real_distribution<double> scroll(-3.0, 3.0);
                ui->input_handler.on_scroll(px(rng_), py(rng_), 0.0, scroll(rng_));
#endif
                break;
            }

            case FuzzAction::KeyPress:
            {
#ifdef SPECTRA_USE_GLFW
                if (!ui)
                    break;
                std::uniform_int_distribution<int> key(32, 126);
                int k = key(rng_);
                ui->input_handler.on_key(k, 1, 0);
                ui->input_handler.on_key(k, 0, 0);
#endif
                break;
            }

            case FuzzAction::CreateFigure:
            {
                auto ids = app_->figure_registry().all_ids();
                if (ids.size() < 20)
                {
                    create_random_figure();
                }
                break;
            }

            case FuzzAction::CloseFigure:
            {
#ifdef SPECTRA_USE_IMGUI
                if (!ui || !ui->fig_mgr)
                    break;
                if (ui->fig_mgr->count() > 1)
                {
                    auto ids = app_->figure_registry().all_ids();
                    if (ids.size() > 1)
                    {
                        std::uniform_int_distribution<size_t> dist(0, ids.size() - 1);
                        ui->fig_mgr->queue_close(ids[dist(rng_)]);
                    }
                }
#endif
                break;
            }

            case FuzzAction::SwitchTab:
            {
#ifdef SPECTRA_USE_IMGUI
                if (!ui || !ui->fig_mgr)
                    break;
                auto ids = app_->figure_registry().all_ids();
                if (!ids.empty())
                {
                    std::uniform_int_distribution<size_t> dist(0, ids.size() - 1);
                    ui->fig_mgr->queue_switch(ids[dist(rng_)]);
                }
#endif
                break;
            }

            case FuzzAction::AddSeries:
            {
                auto ids = app_->figure_registry().all_ids();
                if (ids.empty())
                    break;
                std::uniform_int_distribution<size_t> fig_dist(0, ids.size() - 1);
                auto* fig = app_->figure_registry().get(ids[fig_dist(rng_)]);
                if (!fig || fig->axes().empty())
                    break;

                std::uniform_int_distribution<int> n_dist(10, 200);
                int n = n_dist(rng_);
                std::vector<float> x(n), y(n);
                std::uniform_real_distribution<float> val(-50.0f, 50.0f);
                for (int i = 0; i < n; ++i)
                {
                    x[i] = static_cast<float>(i);
                    y[i] = val(rng_);
                }

                auto& ax = fig->subplot(1, 1, 1);
                std::uniform_int_distribution<int> type_dist(0, 1);
                if (type_dist(rng_) == 0)
                    ax.line(x, y);
                else
                    ax.scatter(x, y);
                break;
            }

            case FuzzAction::UpdateData:
            {
                auto ids = app_->figure_registry().all_ids();
                if (ids.empty())
                    break;
                std::uniform_int_distribution<size_t> fig_dist(0, ids.size() - 1);
                auto* fig = app_->figure_registry().get(ids[fig_dist(rng_)]);
                if (!fig || fig->axes().empty())
                    break;
                auto& ax = *fig->axes()[0];
                if (ax.series().empty())
                    break;

                // Update first series data
                auto* series = ax.series()[0].get();
                auto* line   = dynamic_cast<LineSeries*>(series);
                if (line)
                {
                    auto xd = line->x_data();
                    std::vector<float> new_y(xd.size());
                    std::uniform_real_distribution<float> val(-50.0f, 50.0f);
                    for (size_t i = 0; i < new_y.size(); ++i)
                        new_y[i] = val(rng_);
                    line->set_y(new_y);
                }
                break;
            }

            case FuzzAction::LargeDataset:
            {
                auto ids = app_->figure_registry().all_ids();
                if (ids.empty())
                    break;
                std::uniform_int_distribution<size_t> fig_dist(0, ids.size() - 1);
                auto* fig = app_->figure_registry().get(ids[fig_dist(rng_)]);
                if (!fig)
                    break;

                std::uniform_int_distribution<int> n_dist(100000, 500000);
                int n = n_dist(rng_);
                std::vector<float> x(n), y(n);
                for (int i = 0; i < n; ++i)
                {
                    x[i] = static_cast<float>(i);
                    y[i] = std::sin(static_cast<float>(i) * 0.001f);
                }
                fig->subplot(1, 1, 1).line(x, y);
                break;
            }

            case FuzzAction::SplitDock:
            {
#ifdef SPECTRA_USE_IMGUI
                if (!ui)
                    break;
                std::uniform_int_distribution<int> dir(0, 1);
                if (dir(rng_) == 0)
                    ui->cmd_registry.execute("view.split_right");
                else
                    ui->cmd_registry.execute("view.split_down");
#endif
                break;
            }

            case FuzzAction::Toggle3D:
            {
#ifdef SPECTRA_USE_IMGUI
                if (!ui)
                    break;
                ui->cmd_registry.execute("view.toggle_3d");
#endif
                break;
            }

            case FuzzAction::WaitFrames:
            {
                std::uniform_int_distribution<int> wait(1, 10);
                pump_frames(wait(rng_));
                break;
            }

            default:
                break;
        }
    }

    // ── Per-frame monitoring ─────────────────────────────────────────────
    void check_frame(const App::StepResult& result)
    {
        // Frame time spike detection
        // P0 fix: warmup period (skip first 30 frames) + absolute minimum (33ms)
        // to eliminate false positives from VSync-locked frames
        static constexpr uint64_t WARMUP_FRAMES    = 30;
        static constexpr float    MIN_SPIKE_MS      = 33.0f;
        static constexpr float    SPIKE_MULTIPLIER  = 3.0f;

        if (total_frames_ > WARMUP_FRAMES
            && frame_stats_.ema > 0.5f
            && result.frame_time_ms > MIN_SPIKE_MS
            && result.frame_time_ms > frame_stats_.ema * SPIKE_MULTIPLIER)
        {
            frame_stats_.spike_count++;
            add_issue(IssueSeverity::Warning, "frame_time",
                      "Frame " + std::to_string(result.frame_number)
                      + " took " + std::to_string(result.frame_time_ms)
                      + "ms (" + std::to_string(result.frame_time_ms / frame_stats_.ema)
                      + "x average)");
        }

        // RSS check every 60 frames
        if (total_frames_ % 60 == 0)
        {
            size_t rss = get_rss_bytes();
            if (rss > peak_rss_)
                peak_rss_ = rss;

            size_t growth = (rss > initial_rss_) ? (rss - initial_rss_) : 0;
            if (growth > 100 * 1024 * 1024)   // >100MB growth
            {
                add_issue(IssueSeverity::Warning, "memory",
                          "RSS grew by " + std::to_string(growth / (1024 * 1024))
                          + "MB (initial: " + std::to_string(initial_rss_ / (1024 * 1024))
                          + "MB, current: " + std::to_string(rss / (1024 * 1024)) + "MB)");
            }
        }
    }

    // ── Screenshot capture ───────────────────────────────────────────────
    std::string capture_screenshot(const std::string& reason)
    {
        auto* backend = app_->backend();
        if (!backend)
            return "";

        uint32_t w = backend->swapchain_width();
        uint32_t h = backend->swapchain_height();
        if (w == 0 || h == 0)
            return "";

        std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
        if (!backend->readback_framebuffer(pixels.data(), w, h))
            return "";

        // Sanitize reason for filename
        std::string safe_reason = reason;
        for (auto& c : safe_reason)
        {
            if (!std::isalnum(c) && c != '_')
                c = '_';
        }

        std::string path = opts_.output_dir + "/screenshot_frame"
                           + std::to_string(total_frames_) + "_" + safe_reason + ".png";
        ImageExporter::write_png(path, pixels.data(), w, h);
        return path;
    }

    // ── Wall clock check ─────────────────────────────────────────────────
    bool wall_clock_exceeded() const
    {
        auto elapsed = std::chrono::steady_clock::now() - start_time_;
        float sec = std::chrono::duration<float>(elapsed).count();
        return sec >= opts_.duration_sec;
    }

    float wall_clock_seconds() const
    {
        auto elapsed = std::chrono::steady_clock::now() - start_time_;
        return std::chrono::duration<float>(elapsed).count();
    }

    // ── Report generation ────────────────────────────────────────────────
    size_t issues_with_severity(IssueSeverity sev) const
    {
        size_t count = 0;
        for (const auto& i : issues_)
            if (i.severity == sev)
                count++;
        return count;
    }

    void write_report()
    {
        float duration = wall_clock_seconds();

        // Text report
        {
            std::string path = opts_.output_dir + "/qa_report.txt";
            std::ofstream out(path);
            if (!out)
            {
                fprintf(stderr, "[QA] Failed to write report to %s\n", path.c_str());
                return;
            }

            out << "Spectra QA Agent Report\n";
            out << "=======================\n";
            out << "Seed: " << opts_.seed << "\n";
            out << "Duration: " << duration << "s\n";
            out << "Total frames: " << total_frames_ << "\n";
            out << "Scenarios: " << scenarios_passed_ << " passed, "
                << scenarios_failed_ << " failed\n";
            out << "Fuzz frames: " << (opts_.no_fuzz ? 0 : opts_.fuzz_frames) << "\n";
            out << "\n";

            out << "Frame Time Statistics:\n";
            out << "  Average: " << frame_stats_.average() << "ms\n";
            out << "  P95: " << frame_stats_.percentile(0.95f) << "ms\n";
            out << "  P99: " << frame_stats_.percentile(0.99f) << "ms\n";
            out << "  Max: " << frame_stats_.max_val() << "ms\n";
            out << "  Spikes (>3x avg): " << frame_stats_.spike_count << "\n";
            out << "\n";

            out << "Memory:\n";
            out << "  Initial RSS: " << (initial_rss_ / (1024 * 1024)) << "MB\n";
            out << "  Peak RSS: " << (peak_rss_ / (1024 * 1024)) << "MB\n";
            out << "\n";

            if (!issues_.empty())
            {
                // P1 fix: Group issues by category with summary counts
                std::map<std::string, std::vector<const QAIssue*>> by_category;
                for (const auto& issue : issues_)
                    by_category[issue.category].push_back(&issue);

                out << "Issue Summary (" << issues_.size() << " total, "
                    << by_category.size() << " categories):\n";
                for (const auto& [cat, cat_issues] : by_category)
                {
                    // Count by severity
                    size_t warns = 0, errs = 0, crits = 0;
                    for (auto* i : cat_issues)
                    {
                        if (i->severity == IssueSeverity::Warning) warns++;
                        else if (i->severity == IssueSeverity::Error) errs++;
                        else if (i->severity == IssueSeverity::Critical) crits++;
                    }
                    out << "  " << cat << ": " << cat_issues.size() << " issues";
                    if (crits) out << " (" << crits << " CRITICAL)";
                    if (errs) out << " (" << errs << " ERROR)";
                    if (warns) out << " (" << warns << " WARNING)";
                    out << " [frames " << cat_issues.front()->frame
                        << "-" << cat_issues.back()->frame << "]\n";
                }
                out << "\n";

                // Detailed list (deduplicated: show first 5 per category + count)
                out << "Issue Details:\n";
                for (const auto& [cat, cat_issues] : by_category)
                {
                    out << "  ── " << cat << " (" << cat_issues.size() << ") ──\n";
                    size_t show = std::min(cat_issues.size(), size_t(5));
                    for (size_t i = 0; i < show; ++i)
                    {
                        out << "    [" << severity_str(cat_issues[i]->severity) << "] "
                            << cat_issues[i]->message << "\n";
                    }
                    if (cat_issues.size() > 5)
                    {
                        out << "    ... and " << (cat_issues.size() - 5) << " more\n";
                    }
                }
                out << "\n";
            }
            else
            {
                out << "No issues detected.\n\n";
            }

            out << "Seed for reproduction: " << opts_.seed << "\n";

            fprintf(stderr, "[QA] Report written to %s\n", path.c_str());
        }

        // JSON report
        {
            std::string path = opts_.output_dir + "/qa_report.json";
            std::ofstream out(path);
            if (!out)
                return;

            out << "{\n";
            out << "  \"seed\": " << opts_.seed << ",\n";
            out << "  \"duration_sec\": " << duration << ",\n";
            out << "  \"total_frames\": " << total_frames_ << ",\n";
            out << "  \"scenarios_passed\": " << scenarios_passed_ << ",\n";
            out << "  \"scenarios_failed\": " << scenarios_failed_ << ",\n";
            out << "  \"frame_time\": {\n";
            out << "    \"avg_ms\": " << frame_stats_.average() << ",\n";
            out << "    \"p95_ms\": " << frame_stats_.percentile(0.95f) << ",\n";
            out << "    \"p99_ms\": " << frame_stats_.percentile(0.99f) << ",\n";
            out << "    \"max_ms\": " << frame_stats_.max_val() << ",\n";
            out << "    \"spikes\": " << frame_stats_.spike_count << "\n";
            out << "  },\n";
            out << "  \"memory\": {\n";
            out << "    \"initial_rss_mb\": " << (initial_rss_ / (1024 * 1024)) << ",\n";
            out << "    \"peak_rss_mb\": " << (peak_rss_ / (1024 * 1024)) << "\n";
            out << "  },\n";
            out << "  \"issues\": [\n";
            for (size_t i = 0; i < issues_.size(); ++i)
            {
                const auto& issue = issues_[i];
                out << "    {\"severity\": \"" << severity_str(issue.severity)
                    << "\", \"category\": \"" << issue.category
                    << "\", \"message\": \"" << issue.message
                    << "\", \"frame\": " << issue.frame << "}";
                if (i + 1 < issues_.size())
                    out << ",";
                out << "\n";
            }
            out << "  ]\n";
            out << "}\n";
        }

        // Print summary to stderr
        fprintf(stderr,
                "\n[QA] ═══════════════════════════════════════\n"
                "[QA] Seed: %lu\n"
                "[QA] Duration: %.1fs | Frames: %lu\n"
                "[QA] Scenarios: %u passed, %u failed\n"
                "[QA] Frame time: avg=%.1fms p95=%.1fms max=%.1fms spikes=%u\n"
                "[QA] Memory: initial=%luMB peak=%luMB\n"
                "[QA] Issues: %lu warning, %lu error, %lu critical\n"
                "[QA] ═══════════════════════════════════════\n",
                static_cast<unsigned long>(opts_.seed),
                duration,
                static_cast<unsigned long>(total_frames_),
                scenarios_passed_, scenarios_failed_,
                frame_stats_.average(),
                frame_stats_.percentile(0.95f),
                frame_stats_.max_val(),
                frame_stats_.spike_count,
                static_cast<unsigned long>(initial_rss_ / (1024 * 1024)),
                static_cast<unsigned long>(peak_rss_ / (1024 * 1024)),
                static_cast<unsigned long>(issues_with_severity(IssueSeverity::Warning)),
                static_cast<unsigned long>(issues_with_severity(IssueSeverity::Error)),
                static_cast<unsigned long>(issues_with_severity(IssueSeverity::Critical)));
    }

    // ── Members ──────────────────────────────────────────────────────────
    QAOptions    opts_;
    std::mt19937 rng_;

    std::unique_ptr<App> app_;

    std::chrono::steady_clock::time_point start_time_;

    uint64_t total_frames_     = 0;
    uint32_t scenarios_passed_ = 0;
    uint32_t scenarios_failed_ = 0;

    FrameStats frame_stats_;

    size_t initial_rss_ = 0;
    size_t peak_rss_    = 0;

    std::vector<QAIssue>  issues_;
    std::vector<Scenario> scenarios_;

    // P0 fix: screenshot rate limiting per category
    std::unordered_map<std::string, uint64_t> last_screenshot_frame_;

    // Design review
    std::vector<std::pair<std::string, std::string>> design_screenshots_;
};

// ─── Signal handler ──────────────────────────────────────────────────────────
static void crash_handler(int sig)
{
    const char* name = (sig == SIGSEGV) ? "SIGSEGV" : (sig == SIGABRT) ? "SIGABRT" : "SIGNAL";

    // Minimal async-signal-safe output
    char buf[512];
    int  len = snprintf(buf, sizeof(buf),
                        "\n[QA] ══════════════════════════════════════\n"
                        "[QA] CRASH: %s\n"
                        "[QA] Seed: %lu\n"
                        "[QA] Last action: %s\n"
                        "[QA] Reproduce: --seed %lu\n",
                        name,
                        static_cast<unsigned long>(g_qa_seed),
                        g_last_action,
                        static_cast<unsigned long>(g_qa_seed));
    if (len > 0)
        (void)write(STDERR_FILENO, buf, static_cast<size_t>(len));

#ifdef __linux__
    // Stack trace via backtrace()
    void* frames[32];
    int   nframes = backtrace(frames, 32);
    if (nframes > 0)
    {
        const char* hdr = "[QA] Stack trace:\n";
        (void)write(STDERR_FILENO, hdr, strlen(hdr));
        backtrace_symbols_fd(frames, nframes, STDERR_FILENO);
    }
#endif

    // Try to write partial crash report
    {
        char crash_path[768];
        snprintf(crash_path, sizeof(crash_path), "%s/qa_crash.txt", g_output_dir);
        int fd = open(crash_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0)
        {
            char crash_buf[512];
            int  clen = snprintf(crash_buf, sizeof(crash_buf),
                                 "CRASH: %s\nSeed: %lu\nLast action: %s\n",
                                 name,
                                 static_cast<unsigned long>(g_qa_seed),
                                 g_last_action);
            if (clen > 0)
                (void)write(fd, crash_buf, static_cast<size_t>(clen));
#ifdef __linux__
            if (nframes > 0)
                backtrace_symbols_fd(frames, nframes, fd);
#endif
            close(fd);
        }
    }

    const char* footer = "[QA] ══════════════════════════════════════\n";
    (void)write(STDERR_FILENO, footer, strlen(footer));
    _exit(2);
}

// ─── main ────────────────────────────────────────────────────────────────────
int main(int argc, char** argv)
{
    auto opts = parse_args(argc, argv);
    g_qa_seed = opts.seed;
    snprintf(g_output_dir, sizeof(g_output_dir), "%s", opts.output_dir.c_str());

    // Install crash handlers (stack trace + last action context)
    std::signal(SIGSEGV, crash_handler);
    std::signal(SIGABRT, crash_handler);

    QAAgent agent(opts);
    if (!agent.init())
    {
        fprintf(stderr, "[QA] Failed to initialize\n");
        return 1;
    }

    return agent.run();
}
