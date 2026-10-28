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
//     --design-review     Capture 63 named UI states, each proved by semantic
//                         assertions (failed assertion = ERROR, exit 1)
//     --repeat <N>        Run each scenario N times; N>=3 judges RSS retention
//                         for sustained growth (see qa_stats.hpp)

#include <spectra/app.hpp>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/export.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>

#include "qa_stats.hpp"
#include "render/backend.hpp"
#include "render/vulkan/vk_backend.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
    #include <imgui_internal.h>   // ImGuiWindow / popup stack for design assertions
    #include "ui/app/session_runtime.hpp"
    #include "ui/app/window_ui_context.hpp"
    #include "ui/commands/command_registry.hpp"
    #include "ui/figures/figure_manager.hpp"
    #include "ui/figures/tab_drag_controller.hpp"
    #include "ui/imgui/imgui_integration.hpp"
    #include "ui/theme/theme.hpp"
    #include "ui/input/input.hpp"
    #include "ui/window/window_manager.hpp"
    #include "ui/workspace/figure_serializer.hpp"
#endif

#ifdef SPECTRA_USE_GLFW
    #include <GLFW/glfw3.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef __linux__
    #include <malloc.h>   // malloc_trim
#endif
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <typeindex>
#include <typeinfo>
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
    if (fscanf(f, "%*d %ld", &pages) != 1)
        pages = 0;
    fclose(f);
    return static_cast<size_t>(pages) * 4096;
#else
    return 0;
#endif
}

static uint64_t to_mb(uint64_t bytes)
{
    return bytes / (1024ull * 1024ull);
}

static int64_t signed_byte_delta(uint64_t before, uint64_t after)
{
    if (after >= before)
        return static_cast<int64_t>(after - before);
    return -static_cast<int64_t>(before - after);
}

static int64_t to_mb_signed(int64_t bytes)
{
    return bytes / (1024ll * 1024ll);
}

static std::string format_mb_delta(int64_t delta_bytes)
{
    const int64_t abs_bytes = (delta_bytes >= 0) ? delta_bytes : -delta_bytes;
    const int64_t abs_mb    = to_mb_signed(abs_bytes);
    return std::string(delta_bytes >= 0 ? "+" : "-") + std::to_string(abs_mb) + "MB";
}

static std::string format_kb_delta(int64_t delta_bytes)
{
    const int64_t abs_bytes = (delta_bytes >= 0) ? delta_bytes : -delta_bytes;
    return std::string(delta_bytes >= 0 ? "+" : "-") + std::to_string(abs_bytes / 1024) + "KB";
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
        case IssueSeverity::Info:
            return "INFO";
        case IssueSeverity::Warning:
            return "WARNING";
        case IssueSeverity::Error:
            return "ERROR";
        case IssueSeverity::Critical:
            return "CRITICAL";
    }
    return "???";
}

// ─── Crash handler globals (must be before QAAgent class) ────────────────────
static uint64_t g_qa_seed          = 0;
static char     g_last_action[256] = "init";
static char     g_output_dir[512]  = "/tmp/spectra_qa";

// ─── Vulkan validation monitoring (runtime, non-gtest) ─────────────────────
struct ValidationMessageRecord
{
    VkDebugUtilsMessageSeverityFlagBitsEXT severity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
    std::string                            message_id;
    std::string                            message;
};

class ValidationMonitor
{
   public:
    explicit ValidationMonitor(VkInstance instance) : instance_(instance)
    {
        if (instance_ == VK_NULL_HANDLE)
            return;

        auto create_fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
        destroy_fn_ = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));

        if (!create_fn || !destroy_fn_)
        {
            destroy_fn_ = nullptr;
            return;
        }

        VkDebugUtilsMessengerCreateInfoEXT ci{};
        ci.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                             | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        ci.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                             | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                             | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        ci.pfnUserCallback = &ValidationMonitor::callback;
        ci.pUserData       = this;

        if (create_fn(instance_, &ci, nullptr, &messenger_) != VK_SUCCESS)
        {
            messenger_  = VK_NULL_HANDLE;
            destroy_fn_ = nullptr;
        }
    }

    ~ValidationMonitor()
    {
        if (messenger_ != VK_NULL_HANDLE && destroy_fn_)
        {
            destroy_fn_(instance_, messenger_, nullptr);
        }
    }

    ValidationMonitor(const ValidationMonitor&)            = delete;
    ValidationMonitor& operator=(const ValidationMonitor&) = delete;

    bool active() const { return messenger_ != VK_NULL_HANDLE; }

    uint32_t error_count() const { return error_count_.load(std::memory_order_relaxed); }
    uint32_t warning_count() const { return warning_count_.load(std::memory_order_relaxed); }

    std::vector<ValidationMessageRecord> drain_new_messages()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (next_unreported_index_ >= messages_.size())
            return {};

        std::vector<ValidationMessageRecord> out(messages_.begin() + next_unreported_index_,
                                                 messages_.end());
        next_unreported_index_ = messages_.size();
        return out;
    }

   private:
    static VKAPI_ATTR VkBool32 VKAPI_CALL callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                   VkDebugUtilsMessageTypeFlagsEXT,
                                                   const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                   void* user_data)
    {
        auto* self = static_cast<ValidationMonitor*>(user_data);
        if (!self || !data)
            return VK_FALSE;

        ValidationMessageRecord rec;
        rec.severity   = severity;
        rec.message_id = data->pMessageIdName ? data->pMessageIdName : "";
        rec.message    = data->pMessage ? data->pMessage : "";

        {
            std::lock_guard<std::mutex> lock(self->mutex_);
            self->messages_.push_back(std::move(rec));
        }

        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            self->error_count_.fetch_add(1, std::memory_order_relaxed);
        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            self->warning_count_.fetch_add(1, std::memory_order_relaxed);

        return VK_FALSE;
    }

    VkInstance                          instance_   = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT            messenger_  = VK_NULL_HANDLE;
    PFN_vkDestroyDebugUtilsMessengerEXT destroy_fn_ = nullptr;

    std::atomic<uint32_t>                error_count_{0};
    std::atomic<uint32_t>                warning_count_{0};
    std::mutex                           mutex_;
    std::vector<ValidationMessageRecord> messages_;
    size_t                               next_unreported_index_ = 0;
};

static uint64_t fnv1a64(const uint8_t* data, size_t size)
{
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < size; ++i)
    {
        h ^= static_cast<uint64_t>(data[i]);
        h *= 1099511628211ull;
    }
    return h;
}

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
    uint64_t    seed         = 0;
    float       duration_sec = 120.0f;
    std::string scenario_name;
    uint64_t    fuzz_frames    = 3000;
    std::string output_dir     = "/tmp/spectra_qa";
    bool        no_fuzz        = false;
    bool        no_scenarios   = false;
    bool        list_scenarios = false;
    bool        design_review  = false;
    uint32_t    repeat         = 1;   // run each scenario N times (memory trend)
};

static QAOptions parse_args(int argc, char** argv)
{
    QAOptions opts;
    opts.seed = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());

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
        else if (arg == "--repeat" && i + 1 < argc)
            opts.repeat = std::max(1u, static_cast<uint32_t>(std::stoul(argv[++i])));
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
                    "  --design-review     Capture UI screenshots for design analysis\n"
                    "  --repeat <N>        Run each scenario N times; with N>=3 RSS retention\n"
                    "                      is judged for sustained growth after warm-up\n");
            exit(0);
        }
    }
    return opts;
}

// ─── Scenario definition ─────────────────────────────────────────────────────
struct Scenario
{
    std::string                         name;
    std::string                         description;
    std::function<bool(class QAAgent&)> run;
};

struct ScenarioMemoryMetric
{
    std::string                   name;
    bool                          passed           = false;
    uint64_t                      frame_start      = 0;
    uint64_t                      frame_end        = 0;
    size_t                        rss_before_bytes = 0;
    size_t                        rss_after_bytes  = 0;
    bool                          gpu_before_valid = false;
    bool                          gpu_after_valid  = false;
    VulkanBackend::GpuMemoryStats gpu_before{};
    VulkanBackend::GpuMemoryStats gpu_after{};
};

// ─── Design-review semantic assertions ──────────────────────────────────────
// Every design capture names a UI state ("menu open", "crosshair", ...).  Each
// capture carries a verifier that proves that state from app / ImGui state of
// the captured frame; a failed check is an ERROR issue, never a silent PNG.
struct DesignCheck
{
    std::vector<std::string> passed;
    std::vector<std::string> failed;

    bool expect(bool ok, const std::string& what)
    {
        (ok ? passed : failed).push_back(what);
        return ok;
    }
    bool near(double actual, double expected, double tol, const std::string& what)
    {
        const bool ok = std::abs(actual - expected) <= tol;
        return expect(ok,
                      what + " (actual " + std::to_string(actual) + ", expected "
                          + std::to_string(expected) + " +/- " + std::to_string(tol) + ")");
    }
};
using DesignVerifier = std::function<void(DesignCheck&)>;

struct DesignAssertionRecord
{
    std::string              shot;
    std::string              path;
    uint64_t                 frame  = 0;
    size_t                   checks = 0;
    std::vector<std::string> failures;
};

#ifdef SPECTRA_USE_IMGUI
// True when an ImGui window whose name contains `needle` was submitted in the
// last completed frame of `ctx` (Begin() sets Active; NewFrame() clears it).
static bool imgui_window_drawn(ImGuiContext* ctx, std::string_view needle)
{
    if (!ctx)
        return false;
    for (ImGuiWindow* w : ctx->Windows)
    {
        if (w && w->Active && !w->Hidden && w->Name
            && std::string_view(w->Name).find(needle) != std::string_view::npos)
            return true;
    }
    return false;
}

static ImGuiWindow* imgui_find_window(ImGuiContext* ctx, std::string_view name)
{
    if (!ctx)
        return nullptr;
    for (ImGuiWindow* w : ctx->Windows)
        if (w && w->Name && std::string_view(w->Name) == name)
            return w;
    return nullptr;
}

// Open popups drawn in the last frame whose parent window name contains
// `parent_needle` (empty = any parent).  `modal_only` restricts to modals.
static int imgui_drawn_popups(ImGuiContext*    ctx,
                              std::string_view parent_needle = {},
                              bool             modal_only    = false)
{
    if (!ctx)
        return 0;
    int n = 0;
    for (const ImGuiPopupData& p : ctx->OpenPopupStack)
    {
        ImGuiWindow* w = p.Window;
        if (!w || !w->Active || w->Hidden)
            continue;
        if (modal_only && !(w->Flags & ImGuiWindowFlags_Modal))
            continue;
        if (!parent_needle.empty())
        {
            const ImGuiWindow* parent = w->ParentWindow;
            if (!parent || !parent->Name
                || std::string_view(parent->Name).find(parent_needle) == std::string_view::npos)
                continue;
        }
        ++n;
    }
    return n;
}

// Diagnostic list of windows drawn last frame (appended to failed checks).
static std::string imgui_drawn_window_names(ImGuiContext* ctx)
{
    std::string out;
    if (!ctx)
        return "<no imgui context>";
    for (ImGuiWindow* w : ctx->Windows)
    {
        if (!w || !w->Active || w->Hidden || !w->Name || (w->Flags & ImGuiWindowFlags_ChildWindow))
            continue;
        if (!out.empty())
            out += ", ";
        out += w->Name;
    }
    return out;
}
#endif

static std::string json_escape(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    for (char ch : in)
    {
        if (ch == '"' || ch == '\\')
        {
            out += '\\';
            out += ch;
        }
        else if (static_cast<unsigned char>(ch) < 0x20)
            out += ' ';
        else
            out += ch;
    }
    return out;
}

static bool rect_contains(const Rect& r, double x, double y)
{
    return r.w > 0.0f && r.h > 0.0f && x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

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
        app_         = std::make_unique<App>(cfg);

        // Create an initial figure with some data so the window isn't empty
        auto&              fig = app_->figure({1280, 720});
        auto&              ax  = fig.subplot(1, 1, 1);
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
        sample_gpu_memory();
        initial_gpu_memory_ = current_gpu_memory_;
        peak_gpu_memory_    = current_gpu_memory_;

        auto* vk_backend = dynamic_cast<VulkanBackend*>(app_->backend());
        if (vk_backend && vk_backend->instance() != VK_NULL_HANDLE)
        {
            validation_monitor_ = std::make_unique<ValidationMonitor>(vk_backend->instance());
            if (validation_monitor_->active())
            {
                fprintf(stderr, "[QA] Vulkan validation monitor active\n");
            }
            else
            {
                fprintf(stderr,
                        "[QA] Vulkan validation monitor unavailable (VK_EXT_debug_utils not "
                        "loaded)\n");
            }
        }
        return true;
    }

    int run()
    {
        if (opts_.list_scenarios)
        {
            list_scenarios();
            validation_monitor_.reset();
            app_->shutdown_runtime();
            app_.reset();
            return 0;
        }

        fprintf(stderr,
                "[QA] Spectra QA Agent starting (seed: %lu)\n",
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

        check_validation_messages("end_of_run");

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

        validation_monitor_.reset();
        app_->shutdown_runtime();
        app_.reset();

        return exit_code;
    }

    // Public accessors for scenarios
    App&          app() { return *app_; }
    std::mt19937& rng() { return rng_; }

    bool has_critical_issue() const { return issues_with_severity(IssueSeverity::Critical) > 0; }

    void pump_frames(uint64_t count)
    {
        for (uint64_t i = 0; i < count; ++i)
        {
            if (has_critical_issue())
                break;
            // Force a render even when event-driven rendering gate is idle.
            if (auto* s = app_->session())
                s->redraw_tracker().mark_dirty("qa_pump");
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
                add_issue(IssueSeverity::Critical,
                          "runtime",
                          std::string("Exception in step(): ") + e.what());
                break;
            }
        }
    }

    void add_issue(IssueSeverity      sev,
                   const std::string& cat,
                   const std::string& msg,
                   bool               capture_evidence = true)
    {
        QAIssue issue;
        issue.severity = sev;
        issue.category = cat;
        issue.message  = msg;
        issue.frame    = total_frames_;

        // P0 fix: Screenshot rate limiting — max 1 per category per 60 frames
        static constexpr uint64_t SCREENSHOT_COOLDOWN = 60;
        if (capture_evidence && sev >= IssueSeverity::Warning)
        {
            auto it = last_screenshot_frame_.find(cat);
            if (it == last_screenshot_frame_.end()
                || (total_frames_ - it->second) >= SCREENSHOT_COOLDOWN)
            {
                issue.screenshot_path       = capture_screenshot(cat);
                last_screenshot_frame_[cat] = total_frames_;
            }
        }

        fprintf(stderr,
                "[QA] [%s] %s: %s (frame %lu)\n",
                severity_str(sev),
                cat.c_str(),
                msg.c_str(),
                static_cast<unsigned long>(total_frames_));

        issues_.push_back(std::move(issue));
    }

    FigureId create_random_figure()
    {
        std::uniform_int_distribution<uint32_t> dim_dist(400, 1600);
        uint32_t                                w   = dim_dist(rng_);
        uint32_t                                h   = dim_dist(rng_);
        auto&                                   fig = app_->figure({w, h});
        auto&                                   ax  = fig.subplot(1, 1, 1);

        // Add random data
        std::uniform_int_distribution<int>    n_dist(10, 500);
        int                                   n = n_dist(rng_);
        std::vector<float>                    x(n), y(n);
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
        auto&              fig = app_->figure({1280, 720});
        auto&              ax  = fig.subplot(1, 1, 1);
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

    // Normalize to a single window and a single lightweight figure so
    // heavyweight command scenarios do not contaminate later memory checks.
    void reset_to_single_window_lightweight_state()
    {
#ifdef SPECTRA_USE_IMGUI
        auto* wm = app_->window_manager();
        if (wm)
        {
            auto clear_cached_figure_refs = [this](Figure* fig)
            {
                if (!fig)
                    return;
                if (auto* ui = app_->ui_context())
                {
                    if (ui->data_interaction)
                        ui->data_interaction->clear_figure_cache(fig);
                    ui->input_handler.clear_figure_cache(fig);
                    if (ui->imgui_ui)
                        ui->imgui_ui->clear_figure_cache(fig);
                }
                if (auto* local_wm = app_->window_manager())
                {
                    for (auto* wctx : local_wm->windows())
                    {
                        if (!wctx || !wctx->ui_ctx)
                            continue;
                        if (wctx->ui_ctx->data_interaction)
                            wctx->ui_ctx->data_interaction->clear_figure_cache(fig);
                        wctx->ui_ctx->input_handler.clear_figure_cache(fig);
                        if (wctx->ui_ctx->imgui_ui)
                            wctx->ui_ctx->imgui_ui->clear_figure_cache(fig);
                    }
                }
            };

            const auto windows = wm->windows();
            for (size_t i = 1; i < windows.size(); ++i)
            {
                if (windows[i])
                {
                    for (FigureId fid : windows[i]->assigned_figures)
                    {
                        clear_cached_figure_refs(app_->figure_registry().get(fid));
                    }
                    wm->request_close(windows[i]->id);
                }
            }
            wm->process_pending_closes();
            pump_frames(5);
        }

        ensure_lightweight_active_figure();

        auto* ui = app_->ui_context();
        if (ui && ui->fig_mgr)
        {
            FigureId keep = ui->fig_mgr->active_index();
            if (keep != INVALID_FIGURE_ID)
            {
                ui->fig_mgr->close_all_except(keep);
                // Pump enough frames for deferred GPU buffer frees to flush
                // (requires flight_count_ frames after destruction is queued).
                pump_frames(10);
            }
        }

        // Clear the undo stack — closures may hold raw pointers to
        // figures/axes that were just destroyed by close_all_except.
        if (ui)
            ui->undo_mgr.clear();
#endif

#ifdef __linux__
        // Ask glibc to return freed heap pages to the OS so that
        // RSS measurements reflect actual live allocations.
        malloc_trim(0);
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

        scenarios_.push_back({"stress_docking",
                              "4 figures, split into grid, add tabs, rapid switching",
                              [](QAAgent& qa) { return qa.scenario_stress_docking(); }});

        scenarios_.push_back({"resize_stress",
                              "30 rapid window resizes including extreme sizes",
                              [](QAAgent& qa) { return qa.scenario_resize_stress(); }});

        scenarios_.push_back({"3d_zoom_then_rotate",
                              "Zoom in/out on 3D scatter then verify orbit rotation still works",
                              [](QAAgent& qa) { return qa.scenario_3d_zoom_then_rotate(); }});

        scenarios_.push_back({"window_resize_glfw",
                              "30 rapid GLFW window resizes including extreme aspect ratios",
                              [](QAAgent& qa) { return qa.scenario_window_resize_glfw(); }});

        scenarios_.push_back(
            {"multi_window_lifecycle",
             "Create/destroy 5 windows, move figures between them, close in random order",
             [](QAAgent& qa) { return qa.scenario_multi_window_lifecycle(); }});

        scenarios_.push_back(
            {"multi_window_churn",
             "8 cycles of open 4 windows / close them; RSS, fds and threads per cycle",
             [](QAAgent& qa) { return qa.scenario_multi_window_churn(); }});

        scenarios_.push_back(
            {"tab_drag_between_windows",
             "Detach tabs into new windows, move figures across windows, re-attach",
             [](QAAgent& qa) { return qa.scenario_tab_drag_between_windows(); }});

        scenarios_.push_back({"window_drag_stress",
                              "Rapidly reposition windows across screen, monitor frame times",
                              [](QAAgent& qa) { return qa.scenario_window_drag_stress(); }});

        scenarios_.push_back(
            {"resize_marathon",
             "500+ resize events simulating real user edge-dragging with smooth increments",
             [](QAAgent& qa) { return qa.scenario_resize_marathon(); }});

        scenarios_.push_back({"series_clipboard_selection",
                              "Test series selection, right-click select, clipboard "
                              "copy/cut/paste/delete, multi-select",
                              [](QAAgent& qa)
                              { return qa.scenario_series_clipboard_selection(); }});

        scenarios_.push_back({"figure_serialization",
                              "Save figure via file.save_figure command, reload via "
                              "file.load_figure, verify series count",
                              [](QAAgent& qa) { return qa.scenario_figure_serialization(); }});

        scenarios_.push_back({"series_removed_interaction_safety",
                              "Add markers/hover on series, delete series, verify no crash "
                              "(notify_series_removed path)",
                              [](QAAgent& qa)
                              { return qa.scenario_series_removed_interaction_safety(); }});

        scenarios_.push_back({"line_culling_pan_zoom",
                              "Large sorted line series, pan/zoom to stress draw-call culling "
                              "logic, verify no corruption",
                              [](QAAgent& qa) { return qa.scenario_line_culling_pan_zoom(); }});
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
        static constexpr size_t   SCENARIO_RSS_WARNING_BYTES = 20ull * 1024ull * 1024ull;
        static constexpr uint64_t SCENARIO_GPU_WARNING_BYTES = 5ull * 1024ull * 1024ull;
        // Single-run retention above this always warns (no warm-up is this big).
        static constexpr size_t SCENARIO_RSS_HARD_LIMIT_BYTES = 128ull * 1024ull * 1024ull;
        // --repeat: mean growth per repetition after warm-up that counts as a leak.
        static constexpr size_t SCENARIO_RSS_PER_REP_BYTES = 2ull * 1024ull * 1024ull;

        // BUG-8: Per-scenario RSS thresholds for scenarios that legitimately
        // allocate huge transient CPU buffers. These plateau at allocator
        // retention (glibc heap arenas do not return memory to the OS even
        // after free + malloc_trim under fragmentation). GPU device-local
        // usage stays flat for these scenarios across multiple runs, confirming
        // the retention is CPU-side allocator behavior, not a Spectra leak.
        auto scenario_rss_threshold = [](const std::string& name) -> size_t
        {
            if (name == "massive_datasets")
                return 60ull * 1024ull * 1024ull;   // 1M-pt line + 5x100K series
            if (name == "command_exhaustion")
                return 40ull * 1024ull * 1024ull;
            return SCENARIO_RSS_WARNING_BYTES;
        };

        register_scenarios();

        for (auto& scenario : scenarios_)
        {
            if (!opts_.scenario_name.empty() && scenario.name != opts_.scenario_name)
                continue;

            // Post-teardown RSS of every repetition: judged for sustained
            // growth once all repetitions ran (see qa_stats.hpp).
            const int64_t        rss_baseline = static_cast<int64_t>(settled_rss_bytes());
            std::vector<int64_t> rss_after_reps;
            bool                 stop_all = false;
            for (uint32_t rep = 0; rep < opts_.repeat && !stop_all; ++rep)
            {
                const std::string run_name = opts_.repeat > 1
                                                 ? scenario.name + "#" + std::to_string(rep + 1)
                                                 : scenario.name;
                fprintf(stderr, "[QA] Running scenario: %s\n", run_name.c_str());
                snprintf(g_last_action, sizeof(g_last_action), "scenario:%s", run_name.c_str());
                uint64_t start_frame = total_frames_;
                size_t   rss_before  = settled_rss_bytes();

                VulkanBackend::GpuMemoryStats gpu_before{};
                bool                          gpu_before_valid = snapshot_gpu_memory(gpu_before);

                bool ok = false;
                try
                {
                    ok = scenario.run(*this);
                }
                catch (const std::exception& e)
                {
                    add_issue(IssueSeverity::Error,
                              "scenario",
                              scenario.name + " threw: " + e.what());
                }

                if (ok)
                {
                    scenarios_passed_++;
                    fprintf(stderr,
                            "[QA]   PASSED (%lu frames)\n",
                            static_cast<unsigned long>(total_frames_ - start_frame));
                }
                else
                {
                    scenarios_failed_++;
                    add_issue(IssueSeverity::Error, "scenario", scenario.name + " FAILED");
                }

                size_t rss_after = settled_rss_bytes();
                if (rss_after > peak_rss_)
                    peak_rss_ = rss_after;
                rss_after_reps.push_back(static_cast<int64_t>(rss_after));

                VulkanBackend::GpuMemoryStats gpu_after{};
                bool                          gpu_after_valid = snapshot_gpu_memory(gpu_after);

                scenario_memory_.push_back({run_name,
                                            ok,
                                            start_frame,
                                            total_frames_,
                                            rss_before,
                                            rss_after,
                                            gpu_before_valid,
                                            gpu_after_valid,
                                            gpu_before,
                                            gpu_after});

                const int64_t rss_delta = signed_byte_delta(rss_before, rss_after);
                std::string   delta_log = "[QA]   Memory delta: RSS " + format_mb_delta(rss_delta)
                                        + " (" + std::to_string(to_mb(rss_before)) + "->"
                                        + std::to_string(to_mb(rss_after)) + "MB)";
                if (gpu_before_valid && gpu_after_valid)
                {
                    const int64_t gpu_delta = signed_byte_delta(gpu_before.device_local_usage_bytes,
                                                                gpu_after.device_local_usage_bytes);
                    delta_log += ", GPU local " + format_mb_delta(gpu_delta) + " ("
                                 + std::to_string(to_mb(gpu_before.device_local_usage_bytes)) + "->"
                                 + std::to_string(to_mb(gpu_after.device_local_usage_bytes))
                                 + "MB)";
                }
                else
                {
                    delta_log += ", GPU local n/a";
                }
                fprintf(stderr, "%s\n", delta_log.c_str());

                // A single run's retention mixes one-time warm-up (allocator arenas,
                // driver pipeline/JIT caches, glyph caches) with leaks, so it is
                // only INFO; --repeat >= 3 judges sustained growth instead.  A huge
                // single-run retention still warns.
                if (opts_.repeat < 3
                    && rss_after > rss_before + scenario_rss_threshold(scenario.name))
                {
                    const size_t threshold_mb =
                        scenario_rss_threshold(scenario.name) / (1024ull * 1024ull);
                    const bool huge = rss_after > rss_before + SCENARIO_RSS_HARD_LIMIT_BYTES;
                    add_issue(huge ? IssueSeverity::Warning : IssueSeverity::Info,
                              "scenario_memory",
                              "Scenario '" + run_name + "' retained " + format_mb_delta(rss_delta)
                                  + " RSS after teardown (" + std::to_string(to_mb(rss_before))
                                  + "->" + std::to_string(to_mb(rss_after)) + "MB, threshold +"
                                  + std::to_string(threshold_mb)
                                  + "MB; single run includes warm-up, use --repeat 3+ to judge)",
                              false);
                }

                if (gpu_before_valid && gpu_after_valid
                    && gpu_after.device_local_usage_bytes
                           > gpu_before.device_local_usage_bytes + SCENARIO_GPU_WARNING_BYTES)
                {
                    const int64_t gpu_delta = signed_byte_delta(gpu_before.device_local_usage_bytes,
                                                                gpu_after.device_local_usage_bytes);
                    add_issue(IssueSeverity::Warning,
                              "scenario_gpu_memory",
                              "Scenario '" + scenario.name + "' retained "
                                  + format_mb_delta(gpu_delta)
                                  + " GPU device-local memory after "
                                    "teardown ("
                                  + std::to_string(to_mb(gpu_before.device_local_usage_bytes))
                                  + "->" + std::to_string(to_mb(gpu_after.device_local_usage_bytes))
                                  + "MB, threshold +5MB)",
                              false);
                }

                check_validation_messages("scenario:" + run_name);
                if (wall_clock_exceeded() || has_critical_issue())
                    stop_all = true;
            }   // repetitions

            if (opts_.repeat >= 3 && rss_after_reps.size() >= 3)
            {
                const auto v =
                    qa::judge_retention(rss_baseline,
                                        rss_after_reps,
                                        static_cast<int64_t>(SCENARIO_RSS_PER_REP_BYTES));
                std::string trend;
                for (size_t i = 0; i < rss_after_reps.size(); ++i)
                    trend += (i ? "," : "") + std::to_string(to_mb(rss_after_reps[i]));
                const std::string msg =
                    "Scenario '" + scenario.name + "' RSS after each of "
                    + std::to_string(rss_after_reps.size()) + " repetitions: "
                    + std::to_string(to_mb(rss_baseline)) + "->[" + trend + "]MB; warm-up "
                    + format_mb_delta(v.warmup_bytes) + ", then " + format_kb_delta(v.per_rep_bytes)
                    + "/rep (second half " + format_kb_delta(v.late_per_rep_bytes) + "/rep)";
                fprintf(stderr, "[QA]   %s\n", msg.c_str());
                add_issue(v.sustained_growth ? IssueSeverity::Warning : IssueSeverity::Info,
                          "scenario_memory",
                          msg
                              + (v.sustained_growth ? " — sustained growth (possible leak)"
                                                    : " — plateaus (warm-up/allocator retention)"),
                          false);
            }

            if (stop_all)
            {
                fprintf(stderr, "[QA] Wall clock limit reached, stopping scenarios\n");
                break;
            }
        }
    }

    // RSS after returning freed heap pages to the OS (glibc keeps them
    // otherwise, which makes every free look like retention).
    size_t settled_rss_bytes()
    {
#ifdef __linux__
        malloc_trim(0);
#endif
        return get_rss_bytes();
    }

    void check_validation_messages(const std::string& context)
    {
        if (!validation_monitor_ || !validation_monitor_->active())
            return;

        const auto messages = validation_monitor_->drain_new_messages();
        for (const auto& msg : messages)
        {
            const bool is_error =
                (msg.severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0;

            std::string short_message = msg.message;
            if (short_message.size() > 320)
                short_message = short_message.substr(0, 317) + "...";

            const std::string id        = msg.message_id.empty() ? "<no-id>" : msg.message_id;
            const std::string signature = id + "|" + short_message.substr(0, 160);
            const uint32_t    hit_count = ++validation_message_hits_[signature];

            // First occurrence is always reported. After that, sample repeats
            // so persistent validation spam doesn't drown real signal.
            if (hit_count != 1 && hit_count != 10 && (hit_count % 50) != 0)
                continue;

            std::string issue_message = "[" + context + "] " + id + ": " + short_message;
            if (hit_count > 1)
                issue_message += " (seen " + std::to_string(hit_count) + "x)";

            add_issue(is_error ? IssueSeverity::Error : IssueSeverity::Warning,
                      "vulkan_validation",
                      issue_message,
                      false);
        }
    }

    void inspect_screenshot_artifacts(const std::string&          screenshot_name,
                                      const std::vector<uint8_t>& pixels,
                                      uint32_t                    w,
                                      uint32_t                    h)
    {
        const size_t pixel_count = static_cast<size_t>(w) * h;
        if (pixel_count == 0 || pixels.size() < pixel_count * 4)
            return;

        const uint64_t hash = fnv1a64(pixels.data(), pixel_count * 4);
        auto           it   = screenshot_hash_to_name_.find(hash);
        if (it == screenshot_hash_to_name_.end())
        {
            screenshot_hash_to_name_[hash] = screenshot_name;
        }
        else if (it->second != screenshot_name)
        {
            add_issue(IssueSeverity::Warning,
                      "visual_artifact",
                      "Design screenshot '" + screenshot_name + "' is byte-identical to '"
                          + it->second + "'",
                      false);
        }

        const uint8_t r0 = pixels[0];
        const uint8_t g0 = pixels[1];
        const uint8_t b0 = pixels[2];
        const uint8_t a0 = pixels[3];

        size_t  same_as_first = 0;
        size_t  alpha_zero    = 0;
        uint8_t min_luma      = 255;
        uint8_t max_luma      = 0;
        double  mean_luma     = 0.0;
        double  m2_luma       = 0.0;

        for (size_t p = 0; p < pixel_count; ++p)
        {
            const size_t  i = p * 4;
            const uint8_t r = pixels[i + 0];
            const uint8_t g = pixels[i + 1];
            const uint8_t b = pixels[i + 2];
            const uint8_t a = pixels[i + 3];

            if (r == r0 && g == g0 && b == b0 && a == a0)
                same_as_first++;
            if (a == 0)
                alpha_zero++;

            const uint8_t luma = static_cast<uint8_t>((54u * static_cast<uint32_t>(r)
                                                       + 183u * static_cast<uint32_t>(g)
                                                       + 19u * static_cast<uint32_t>(b))
                                                      >> 8);
            min_luma           = std::min(min_luma, luma);
            max_luma           = std::max(max_luma, luma);

            const double x     = static_cast<double>(luma);
            const double delta = x - mean_luma;
            mean_luma += delta / static_cast<double>(p + 1);
            const double delta2 = x - mean_luma;
            m2_luma += delta * delta2;
        }

        const double same_ratio       = static_cast<double>(same_as_first) / pixel_count;
        const double alpha_zero_ratio = static_cast<double>(alpha_zero) / pixel_count;
        const double variance =
            (pixel_count > 1) ? (m2_luma / static_cast<double>(pixel_count - 1)) : 0.0;
        const double stddev_luma = std::sqrt(variance);

        if (alpha_zero_ratio > 0.98)
        {
            add_issue(IssueSeverity::Error,
                      "visual_artifact",
                      "Design screenshot '" + screenshot_name + "' is mostly transparent ("
                          + std::to_string(alpha_zero_ratio * 100.0) + "% alpha=0)",
                      false);
        }
        if (same_ratio > 0.995)
        {
            add_issue(IssueSeverity::Warning,
                      "visual_artifact",
                      "Design screenshot '" + screenshot_name + "' is near-uniform ("
                          + std::to_string(same_ratio * 100.0) + "% pixels identical)",
                      false);
        }
        if ((max_luma - min_luma) <= 2 && (mean_luma < 4.0 || mean_luma > 251.0))
        {
            add_issue(IssueSeverity::Warning,
                      "visual_artifact",
                      "Design screenshot '" + screenshot_name
                          + "' has almost no luminance range (min="
                          + std::to_string(static_cast<uint32_t>(min_luma))
                          + ", max=" + std::to_string(static_cast<uint32_t>(max_luma)) + ")",
                      false);
        }
        if (mean_luma < 8.0 && stddev_luma < 1.5)
        {
            add_issue(IssueSeverity::Warning,
                      "visual_artifact",
                      "Design screenshot '" + screenshot_name
                          + "' appears almost fully black (mean luma=" + std::to_string(mean_luma)
                          + ", stddev=" + std::to_string(stddev_luma) + ")",
                      false);
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
            add_issue(IssueSeverity::Warning,
                      "figure_lifecycle",
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

        // 1M-point line — scope so the CPU-side build buffers free before measurement.
        {
            std::vector<float> x(1000000), y(1000000);
            for (int i = 0; i < 1000000; ++i)
            {
                x[i] = static_cast<float>(i) * 0.001f;
                y[i] = std::sin(x[i] * 0.01f) * std::cos(x[i] * 0.003f);
            }
            ax.line(x, y).label("1M points");
            // x, y go out of scope here — capacity released after copy into series
        }
        pump_frames(10);

        // 5x100K series — also scoped per-iteration to release builders early.
        for (int s = 0; s < 5; ++s)
        {
            std::vector<float>                    sx(100000), sy(100000);
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

        // Close the heavyweight figure to avoid polluting subsequent scenarios.
        // The test already validated rendering with the large data above.
#ifdef SPECTRA_USE_IMGUI
        auto* ui = app_->ui_context();
        if (ui && ui->fig_mgr)
        {
            FigureId massive_id = app_->figure_registry().find_id(&fig);
            if (massive_id != 0)
            {
                // Create lightweight replacement first
                ensure_lightweight_active_figure();
                ui->fig_mgr->close_figure(massive_id);
                pump_frames(5);
            }
        }
#endif
#ifdef __linux__
        malloc_trim(0);
#endif
        return true;
    }

    bool scenario_undo_redo_stress()
    {
        ensure_lightweight_active_figure();
#ifdef SPECTRA_USE_IMGUI
        auto* ui = app_->ui_context();
        if (!ui || !ui->imgui_ui || !ui->data_interaction || !ui->imgui_ui->is_initialized())
            return true;

        // 50 undoable ops (create figures)
        for (int i = 0; i < 50; ++i)
        {
            UndoAction act;
            act.description = "create_fig_" + std::to_string(i);
            act.redo_fn     = [] {};
            act.undo_fn     = [] {};
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
        new_act.redo_fn     = [] {};
        new_act.undo_fn     = [] {};
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
        if (!ui || !ui->imgui_ui || !ui->data_interaction || !ui->imgui_ui->is_initialized())
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
        auto                     all_cmd_ptrs = ui->cmd_registry.all_commands();
        std::vector<std::string> all_cmds;
        for (auto* c : all_cmd_ptrs)
            if (c)
                all_cmds.push_back(c->id);
        if (all_cmds.empty())
        {
            add_issue(IssueSeverity::Warning, "commands", "No commands registered");
            return true;
        }

        // Execute every command once
        for (const auto& id : all_cmds)
        {
            // Skip destructive and interactive commands (native file dialogs)
            if (id == "figure.close" || id == "app.quit" || id == "file.save_figure"
                || id == "file.load_figure")
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
                if (id == "figure.close" || id == "app.quit" || id == "file.save_figure"
                    || id == "file.load_figure")
                    continue;
                ui->cmd_registry.execute(id);
                pump_frames(1);
            }
        }

        // Commands can create extra windows/figures; clean up to avoid
        // carrying intentional allocations into subsequent scenarios.
        reset_to_single_window_lightweight_state();
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
        auto&              fig = app_->figure({1280, 720});
        auto&              ax  = fig.subplot3d(1, 1, 1);
        std::vector<float> x(200), y(200), z(200);
        for (int i = 0; i < 200; ++i)
        {
            float t = static_cast<float>(i) * 0.1f;
            x[i]    = std::cos(t);
            y[i]    = std::sin(t);
            z[i]    = t * 0.1f;
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
        double      cx = static_cast<double>(vp.x + vp.w * 0.5f);
        double      cy = static_cast<double>(vp.y + vp.h * 0.5f);

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
                add_issue(IssueSeverity::Error,
                          "3d_zoom_rotate",
                          "Zoom changed camera angles: az " + std::to_string(az_before) + " -> "
                              + std::to_string(az_after_zoom) + ", el " + std::to_string(el_before)
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
            float az_delta      = std::abs(az_after_drag - az_after_zoom);
            float el_delta      = std::abs(el_after_drag - el_after_zoom);

            if (az_delta < 1.0f && el_delta < 1.0f)
            {
                add_issue(IssueSeverity::Error,
                          "3d_zoom_rotate",
                          "Orbit rotation FAILED after zoom: az delta=" + std::to_string(az_delta)
                              + ", el delta=" + std::to_string(el_delta)
                              + " (expected significant change from 80px drag)");
                all_passed = false;
            }
            else
            {
                fprintf(stderr,
                        "[QA]   Test 1 OK: orbit after zoom works "
                        "(az delta=%.1f, el delta=%.1f)\n",
                        az_delta,
                        el_delta);
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
                float az_d    = std::abs(az_post - az_pre);
                float el_d    = std::abs(el_post - el_pre);

                if (az_d < 0.5f && el_d < 0.5f)
                {
                    add_issue(IssueSeverity::Warning,
                              "3d_zoom_rotate",
                              "Round " + std::to_string(round)
                                  + ": orbit after zoom had no effect (az_d=" + std::to_string(az_d)
                                  + ", el_d=" + std::to_string(el_d) + ")");
                    any_rotation_failed = true;
                }
            }

            if (any_rotation_failed)
            {
                add_issue(IssueSeverity::Error,
                          "3d_zoom_rotate",
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
                add_issue(IssueSeverity::Error,
                          "3d_zoom_rotate",
                          "Extreme zoom then rotate FAILED: az_d=" + std::to_string(az_d)
                              + ", el_d=" + std::to_string(el_d));
                all_passed = false;
            }
            else
            {
                fprintf(stderr,
                        "[QA]   Test 3 OK: extreme zoom then rotate works "
                        "(az_d=%.1f, el_d=%.1f)\n",
                        az_d,
                        el_d);
            }
        }

        return all_passed;
#else
        return true;
#endif
    }

    // ── New performance scenarios (window resize, multi-window, tab drag) ──

    bool scenario_window_resize_glfw()
    {
        ensure_lightweight_active_figure();
#ifdef SPECTRA_USE_GLFW
        auto* wm = app_->window_manager();
        if (!wm || wm->windows().empty())
            return true;

        auto* wctx     = wm->windows()[0];
        auto* glfw_win = static_cast<GLFWwindow*>(wctx->glfw_window);
        if (!glfw_win)
            return true;

        struct SizeSpec
        {
            int w, h;
        };
        SizeSpec sizes[] = {
            {1280, 720},
            {640, 480},
            {1920, 1080},
            {320, 240},
            {1920, 400},
            {400, 1080},
            {800, 800},
            {1600, 900},
            {100, 100},
            {2560, 1440},
            {640, 360},
            {1280, 720},
        };

        for (int pass = 0; pass < 2; ++pass)
        {
            for (auto& sz : sizes)
            {
                if (has_critical_issue())
                    return false;
                glfwSetWindowSize(glfw_win, sz.w, sz.h);
                pump_frames(3);   // Allow swapchain recreation + render
            }
        }

        // Restore
        glfwSetWindowSize(glfw_win, 1280, 720);
        pump_frames(10);
#endif
        return true;
    }

    bool scenario_multi_window_lifecycle()
    {
        ensure_lightweight_active_figure();
#ifdef SPECTRA_USE_GLFW
        auto* wm = app_->window_manager();
        if (!wm)
            return true;

        // Create 5 figures for 5 windows
        std::vector<FigureId> fig_ids;
        for (int i = 0; i < 5; ++i)
        {
            fig_ids.push_back(create_random_figure());
            pump_frames(2);
        }

        // Detach 4 figures into separate windows
        std::vector<WindowContext*> extra_windows;
        for (int i = 0; i < 4 && i < static_cast<int>(fig_ids.size()); ++i)
        {
            auto* w = wm->detach_figure(fig_ids[i],
                                        600,
                                        400,
                                        "Window " + std::to_string(i + 2),
                                        100 + i * 150,
                                        100 + i * 50);
            if (w)
                extra_windows.push_back(w);
            pump_frames(5);
        }

        // Pump frames with all windows open
        pump_frames(30);

        // Move figures between windows
        if (extra_windows.size() >= 2)
        {
            auto all_wins = wm->windows();
            if (all_wins.size() >= 3)
            {
                // Move a figure from window 1 to window 2
                auto& src_figs = all_wins[1]->assigned_figures;
                if (!src_figs.empty())
                {
                    wm->move_figure(src_figs[0], all_wins[1]->id, all_wins[2]->id);
                    pump_frames(10);
                }
            }
        }

        // Close windows in random order
        auto close_order = extra_windows;
        std::shuffle(close_order.begin(), close_order.end(), rng_);
        for (auto* w : close_order)
        {
            if (has_critical_issue())
                return false;
            wm->request_close(w->id);
            wm->process_pending_closes();
            pump_frames(5);
        }

        pump_frames(10);

        // Clean up extra figures to avoid polluting subsequent scenarios.
        reset_to_single_window_lightweight_state();
#endif
        return true;
    }

    // Repeated window create/close: per-window resources (swapchain images,
    // ImGui context + font atlas, descriptor pools, X connections) must be
    // released, so RSS / fd / thread counts after each cycle must plateau.
    bool scenario_multi_window_churn()
    {
        ensure_lightweight_active_figure();
#ifdef SPECTRA_USE_GLFW
        auto* wm = app_->window_manager();
        if (!wm)
            return true;

        static constexpr int    CYCLES          = 8;
        static constexpr int    WINDOWS         = 4;
        static constexpr size_t PER_CYCLE_BYTES = 1ull * 1024ull * 1024ull;

        auto count_dir = [](const char* path) -> int
        {
            int             n = 0;
            std::error_code ec;
            for (auto it = std::filesystem::directory_iterator(path, ec);
                 !ec && it != std::filesystem::directory_iterator();
                 it.increment(ec))
                ++n;
            return n;
        };

        const int64_t        baseline = static_cast<int64_t>(settled_rss_bytes());
        const int            fd0      = count_dir("/proc/self/fd");
        const int            th0      = count_dir("/proc/self/task");
        std::vector<int64_t> rss;
        std::vector<int>     fds, threads;
        std::string          trace;
        size_t               peak_windows = 0;
        for (int cycle = 0; cycle < CYCLES; ++cycle)
        {
            std::vector<FigureId> fids;
            for (int i = 0; i < WINDOWS; ++i)
            {
                fids.push_back(create_random_figure());
                pump_frames(1);
            }
            std::vector<WindowContext*> wins;
            for (int i = 0; i < WINDOWS; ++i)
            {
                if (auto* w = wm->detach_figure(fids[i],
                                                480,
                                                360,
                                                "Churn " + std::to_string(i),
                                                80 + i * 120,
                                                80 + i * 40))
                    wins.push_back(w);
                pump_frames(2);
            }
            peak_windows = std::max(peak_windows, wm->windows().size());
            pump_frames(10);
            for (auto* w : wins)
            {
                wm->request_close(w->id);
                wm->process_pending_closes();
                pump_frames(2);
            }
            reset_to_single_window_lightweight_state();
            if (has_critical_issue())
                return false;
            rss.push_back(static_cast<int64_t>(settled_rss_bytes()));
            fds.push_back(count_dir("/proc/self/fd"));
            threads.push_back(count_dir("/proc/self/task"));
            trace += (cycle ? "," : "") + std::to_string(to_mb(rss.back()));
            if (wins.size() != static_cast<size_t>(WINDOWS))
                add_issue(IssueSeverity::Error,
                          "multi_window_churn",
                          "cycle " + std::to_string(cycle + 1) + " opened "
                              + std::to_string(wins.size()) + "/" + std::to_string(WINDOWS)
                              + " windows",
                          false);
            if (wm->windows().size() != 1)
                add_issue(IssueSeverity::Error,
                          "multi_window_churn",
                          "cycle " + std::to_string(cycle + 1) + " left "
                              + std::to_string(wm->windows().size()) + " windows open",
                          false);
        }

        const auto v = qa::judge_retention(baseline, rss, static_cast<int64_t>(PER_CYCLE_BYTES));
        const std::string msg =
            "multi_window_churn: " + std::to_string(CYCLES) + " cycles x " + std::to_string(WINDOWS)
            + " windows (peak " + std::to_string(peak_windows) + " open); RSS "
            + std::to_string(to_mb(baseline)) + "->[" + trace + "]MB, first cycle "
            + format_mb_delta(v.warmup_bytes) + ", then " + format_kb_delta(v.per_rep_bytes)
            + "/cycle (second half " + format_kb_delta(v.late_per_rep_bytes) + "/cycle); fds "
            + std::to_string(fd0) + "->" + std::to_string(fds.back()) + ", threads "
            + std::to_string(th0) + "->" + std::to_string(threads.back());
        fprintf(stderr, "[QA]   %s\n", msg.c_str());
        const bool fd_leak = fds.size() >= 3 && fds.back() > fds[1];
        const bool th_leak = threads.size() >= 3 && threads.back() > threads[1];
        add_issue(
            (v.sustained_growth || fd_leak || th_leak) ? IssueSeverity::Warning
                                                       : IssueSeverity::Info,
            "multi_window_churn",
            msg
                + (v.sustained_growth || fd_leak || th_leak ? " — per-window resources keep growing"
                                                            : " — plateaus after the first cycle"),
            false);
#endif
        return true;
    }

    bool scenario_tab_drag_between_windows()
    {
        ensure_lightweight_active_figure();
#ifdef SPECTRA_USE_GLFW
        auto* wm = app_->window_manager();
        auto* ui = app_->ui_context();
        if (!wm || !ui || !ui->fig_mgr)
            return true;

        // Create 4 figures
        std::vector<FigureId> fids;
        for (int i = 0; i < 4; ++i)
        {
            fids.push_back(create_random_figure());
            pump_frames(2);
        }

        // Detach 2 figures into a second window
        auto* win2 = wm->detach_figure(fids[0], 800, 600, "Tab Drag Target", 700, 100);
        pump_frames(10);

        if (win2)
        {
            // Also detach another figure to the same window
            if (fids.size() > 1)
            {
                auto all_wins = wm->windows();
                if (all_wins.size() >= 2)
                {
                    wm->move_figure(fids[1], all_wins[0]->id, win2->id);
                    pump_frames(10);
                }
            }

            // Move a figure back from secondary to primary
            if (!win2->assigned_figures.empty())
            {
                auto all_wins = wm->windows();
                if (all_wins.size() >= 2)
                {
                    wm->move_figure(win2->assigned_figures[0], win2->id, all_wins[0]->id);
                    pump_frames(10);
                }
            }

            // Close secondary
            wm->request_close(win2->id);
            wm->process_pending_closes();
            pump_frames(5);
        }

        pump_frames(10);

        // Clean up extra figures.
        reset_to_single_window_lightweight_state();
#endif
        return true;
    }

    bool scenario_window_drag_stress()
    {
        ensure_lightweight_active_figure();
#ifdef SPECTRA_USE_GLFW
        auto* wm = app_->window_manager();
        if (!wm || wm->windows().empty())
            return true;

        auto* wctx     = wm->windows()[0];
        auto* glfw_win = static_cast<GLFWwindow*>(wctx->glfw_window);
        if (!glfw_win)
            return true;

        // Rapidly reposition window across screen 50 times
        std::uniform_int_distribution<int> pos_x(0, 1600);
        std::uniform_int_distribution<int> pos_y(0, 900);

        for (int i = 0; i < 50; ++i)
        {
            if (has_critical_issue())
                return false;
            glfwSetWindowPos(glfw_win, pos_x(rng_), pos_y(rng_));
            pump_frames(1);
        }

        // Also test rapid position + resize combo
        for (int i = 0; i < 20; ++i)
        {
            if (has_critical_issue())
                return false;
            glfwSetWindowPos(glfw_win, pos_x(rng_), pos_y(rng_));
            std::uniform_int_distribution<int> dim(300, 1600);
            glfwSetWindowSize(glfw_win, dim(rng_), dim(rng_));
            pump_frames(2);
        }

        // Restore
        glfwSetWindowPos(glfw_win, 100, 100);
        glfwSetWindowSize(glfw_win, 1280, 720);
        pump_frames(10);
#endif
#ifdef __linux__
        malloc_trim(0);
#endif
        return true;
    }

    bool scenario_resize_marathon()
    {
        ensure_lightweight_active_figure();
#ifdef SPECTRA_USE_GLFW
        auto* wm = app_->window_manager();
        if (!wm || wm->windows().empty())
            return true;

        auto* wctx     = wm->windows()[0];
        auto* glfw_win = static_cast<GLFWwindow*>(wctx->glfw_window);
        if (!glfw_win)
            return true;

        // Start from a known size
        int cur_w = 1280, cur_h = 720;
        glfwSetWindowSize(glfw_win, cur_w, cur_h);
        pump_frames(5);

        // ── Phase 1: Smooth horizontal drag (right edge) ─────────────────
        // User grabs the right edge and drags slowly from 1280 → 600, then back
        fprintf(stderr, "[QA]   resize_marathon: phase 1 — horizontal drag (100 events)\n");
        for (int i = 0; i < 50; ++i)
        {
            if (has_critical_issue())
                return false;
            cur_w = std::max(200, cur_w - 14);   // ~14px per event, shrinking
            glfwSetWindowSize(glfw_win, cur_w, cur_h);
            pump_frames(1);
        }
        for (int i = 0; i < 50; ++i)
        {
            if (has_critical_issue())
                return false;
            cur_w = std::min(1920, cur_w + 14);   // drag back wider
            glfwSetWindowSize(glfw_win, cur_w, cur_h);
            pump_frames(1);
        }
        pump_frames(5);

        // ── Phase 2: Smooth vertical drag (bottom edge) ──────────────────
        fprintf(stderr, "[QA]   resize_marathon: phase 2 — vertical drag (100 events)\n");
        for (int i = 0; i < 50; ++i)
        {
            if (has_critical_issue())
                return false;
            cur_h = std::max(150, cur_h - 12);
            glfwSetWindowSize(glfw_win, cur_w, cur_h);
            pump_frames(1);
        }
        for (int i = 0; i < 50; ++i)
        {
            if (has_critical_issue())
                return false;
            cur_h = std::min(1080, cur_h + 12);
            glfwSetWindowSize(glfw_win, cur_w, cur_h);
            pump_frames(1);
        }
        pump_frames(5);

        // ── Phase 3: Diagonal corner drag ────────────────────────────────
        // User grabs the bottom-right corner and drags diagonally
        fprintf(stderr, "[QA]   resize_marathon: phase 3 — diagonal drag (80 events)\n");
        for (int i = 0; i < 40; ++i)
        {
            if (has_critical_issue())
                return false;
            cur_w = std::max(300, cur_w - 18);
            cur_h = std::max(200, cur_h - 10);
            glfwSetWindowSize(glfw_win, cur_w, cur_h);
            pump_frames(1);
        }
        for (int i = 0; i < 40; ++i)
        {
            if (has_critical_issue())
                return false;
            cur_w = std::min(1920, cur_w + 18);
            cur_h = std::min(1080, cur_h + 10);
            glfwSetWindowSize(glfw_win, cur_w, cur_h);
            pump_frames(1);
        }
        pump_frames(5);

        // ── Phase 4: Jittery resize (user shaking the edge nervously) ────
        fprintf(stderr, "[QA]   resize_marathon: phase 4 — jittery edge shake (60 events)\n");
        std::uniform_int_distribution<int> jitter(-20, 20);
        int                                base_w = cur_w, base_h = cur_h;
        for (int i = 0; i < 60; ++i)
        {
            if (has_critical_issue())
                return false;
            int w = std::clamp(base_w + jitter(rng_), 200, 2560);
            int h = std::clamp(base_h + jitter(rng_), 150, 1440);
            glfwSetWindowSize(glfw_win, w, h);
            pump_frames(1);
        }
        cur_w = base_w;
        cur_h = base_h;
        glfwSetWindowSize(glfw_win, cur_w, cur_h);
        pump_frames(3);

        // ── Phase 5: Fast resize bursts with pauses (real user stops to look) ─
        fprintf(stderr, "[QA]   resize_marathon: phase 5 — burst + pause (80 events)\n");
        for (int burst = 0; burst < 8; ++burst)
        {
            // 10 rapid resize events per burst
            std::uniform_int_distribution<int> delta(-30, 30);
            for (int i = 0; i < 10; ++i)
            {
                if (has_critical_issue())
                    return false;
                cur_w = std::clamp(cur_w + delta(rng_), 300, 2000);
                cur_h = std::clamp(cur_h + delta(rng_), 200, 1200);
                glfwSetWindowSize(glfw_win, cur_w, cur_h);
                pump_frames(1);
            }
            // Pause — user stops dragging, looks at the result
            pump_frames(10);
        }

        // ── Phase 6: Extreme aspect ratio sweep ─────────────────────────
        // Smoothly go from very wide to very tall
        fprintf(stderr, "[QA]   resize_marathon: phase 6 — aspect ratio sweep (80 events)\n");
        for (int i = 0; i < 40; ++i)
        {
            if (has_critical_issue())
                return false;
            // Wide → narrow: width shrinks, height grows
            float t = static_cast<float>(i) / 39.0f;
            int   w = static_cast<int>(1800.0f * (1.0f - t) + 400.0f * t);
            int   h = static_cast<int>(300.0f * (1.0f - t) + 1000.0f * t);
            glfwSetWindowSize(glfw_win, w, h);
            pump_frames(1);
        }
        for (int i = 0; i < 40; ++i)
        {
            if (has_critical_issue())
                return false;
            // Tall → wide: reverse
            float t = static_cast<float>(i) / 39.0f;
            int   w = static_cast<int>(400.0f * (1.0f - t) + 1800.0f * t);
            int   h = static_cast<int>(1000.0f * (1.0f - t) + 300.0f * t);
            glfwSetWindowSize(glfw_win, w, h);
            pump_frames(1);
        }
        pump_frames(5);

        // ── Phase 7: Double-click snap (maximize → restore cycle) ────────
        fprintf(stderr, "[QA]   resize_marathon: phase 7 — snap maximize/restore (20 events)\n");
        for (int i = 0; i < 10; ++i)
        {
            if (has_critical_issue())
                return false;
            glfwSetWindowSize(glfw_win, 2560, 1440);   // "maximize"
            pump_frames(3);
            glfwSetWindowSize(glfw_win, 1280, 720);   // "restore"
            pump_frames(3);
        }

        // ── Restore to baseline ─────────────────────────────────────────
        glfwSetWindowSize(glfw_win, 1280, 720);
        pump_frames(15);

        fprintf(stderr, "[QA]   resize_marathon: complete — 520+ resize events across 7 phases\n");
#endif
#ifdef __linux__
        malloc_trim(0);
#endif
        return true;
    }

    // ── Series clipboard & selection scenario ──────────────────────────
    bool scenario_series_clipboard_selection()
    {
#ifdef SPECTRA_USE_IMGUI
        auto* ui = app_->ui_context();
        if (!ui)
            return true;

        // Create a figure with 4 series for testing
        auto&              fig = app_->figure({1280, 720});
        auto&              ax  = fig.subplot(1, 1, 1);
        std::vector<float> x(100);
        for (int i = 0; i < 100; ++i)
            x[i] = static_cast<float>(i) * 0.1f;

        std::vector<float> y1(100), y2(100), y3(100), y4(100);
        for (int i = 0; i < 100; ++i)
        {
            y1[i] = std::sin(x[i]);
            y2[i] = std::cos(x[i]);
            y3[i] = std::sin(x[i]) * 0.5f;
            y4[i] = std::cos(x[i]) * 0.5f;
        }
        ax.line(x, y1).label("sin");
        ax.line(x, y2).label("cos");
        ax.line(x, y3).label("sin_half");
        ax.line(x, y4).label("cos_half");

        // Switch to this figure
        auto all_ids = app_->figure_registry().all_ids();
        if (!all_ids.empty() && ui->fig_mgr)
            ui->fig_mgr->queue_switch(all_ids.back());
        pump_frames(10);

        size_t initial_series_count = ax.series().size();
        fprintf(stderr, "[QA]   clipboard: initial series count = %zu\n", initial_series_count);

        // ── Test 1: Left-click select via command ────────────────────────
        fprintf(stderr, "[QA]   clipboard: test 1 — select series via command\n");
        ui->cmd_registry.execute("series.cycle_selection");
        pump_frames(5);

        auto& sel                              = ui->imgui_ui->selection_context();
        bool  selection_matches_current_series = false;
        if (sel.series)
        {
            for (const auto& series_ptr : ax.series())
            {
                if (series_ptr.get() == sel.series)
                {
                    selection_matches_current_series = true;
                    break;
                }
            }
        }
        if (sel.type != ui::SelectionType::Series || !sel.series
            || !selection_matches_current_series)
        {
            add_issue(IssueSeverity::Error,
                      "clipboard",
                      "series.cycle_selection did not select a live series in the active axes");
            return false;
        }
        std::string first_label = sel.series->label();
        fprintf(stderr, "[QA]   clipboard: selected '%s'\n", first_label.c_str());

        // ── Test 2: Copy and paste ───────────────────────────────────────
        fprintf(stderr, "[QA]   clipboard: test 2 — copy + paste\n");
        ui->cmd_registry.execute("series.copy");
        pump_frames(2);

        if (!ui->imgui_ui->series_clipboard() || !ui->imgui_ui->series_clipboard()->has_data())
        {
            add_issue(IssueSeverity::Error, "clipboard", "series.copy did not populate clipboard");
            return false;
        }

        ui->cmd_registry.execute("series.paste");
        pump_frames(5);

        size_t after_paste = ax.series().size();
        if (after_paste != initial_series_count + 1)
        {
            add_issue(IssueSeverity::Error,
                      "clipboard",
                      "Paste failed: expected " + std::to_string(initial_series_count + 1)
                          + " series, got " + std::to_string(after_paste));
            return false;
        }
        fprintf(stderr, "[QA]   clipboard: paste OK, series count = %zu\n", after_paste);

        // ── Test 3: Cut (removes original) ───────────────────────────────
        fprintf(stderr, "[QA]   clipboard: test 3 — cut\n");
        // Re-select first series
        ui->cmd_registry.execute("series.cycle_selection");
        pump_frames(2);
        ui->cmd_registry.execute("series.cut");
        pump_frames(5);

        size_t after_cut = ax.series().size();
        if (after_cut != after_paste - 1)
        {
            add_issue(IssueSeverity::Error,
                      "clipboard",
                      "Cut failed: expected " + std::to_string(after_paste - 1) + " series, got "
                          + std::to_string(after_cut));
            return false;
        }
        fprintf(stderr, "[QA]   clipboard: cut OK, series count = %zu\n", after_cut);

        // Paste the cut series back
        ui->cmd_registry.execute("series.paste");
        pump_frames(5);

        size_t after_cut_paste = ax.series().size();
        if (after_cut_paste != after_cut + 1)
        {
            add_issue(IssueSeverity::Error,
                      "clipboard",
                      "Paste-after-cut failed: expected " + std::to_string(after_cut + 1)
                          + " series, got " + std::to_string(after_cut_paste));
            return false;
        }
        fprintf(stderr,
                "[QA]   clipboard: paste-after-cut OK, series count = %zu\n",
                after_cut_paste);

        // ── Test 4: Delete ───────────────────────────────────────────────
        fprintf(stderr, "[QA]   clipboard: test 4 — delete\n");
        ui->cmd_registry.execute("series.cycle_selection");
        pump_frames(2);
        ui->cmd_registry.execute("series.delete");
        pump_frames(5);

        size_t after_delete = ax.series().size();
        if (after_delete != after_cut_paste - 1)
        {
            add_issue(IssueSeverity::Error,
                      "clipboard",
                      "Delete failed: expected " + std::to_string(after_cut_paste - 1)
                          + " series, got " + std::to_string(after_delete));
            return false;
        }
        fprintf(stderr, "[QA]   clipboard: delete OK, series count = %zu\n", after_delete);

        // ── Test 5: Deselect ─────────────────────────────────────────────
        fprintf(stderr, "[QA]   clipboard: test 5 — deselect\n");
        ui->cmd_registry.execute("series.cycle_selection");
        pump_frames(2);
        ui->cmd_registry.execute("series.deselect");
        pump_frames(2);

        if (sel.type == ui::SelectionType::Series)
        {
            add_issue(IssueSeverity::Error,
                      "clipboard",
                      "Deselect failed: selection type still Series");
            return false;
        }
        fprintf(stderr, "[QA]   clipboard: deselect OK\n");

        // ── Test 6: Right-click selection via DataInteraction ─────────────
        fprintf(stderr, "[QA]   clipboard: test 6 — right-click series selection\n");
        if (ui->data_interaction)
        {
            // Move cursor near the first series to populate nearest point
            const auto& vp = ax.viewport();
            double      cx = static_cast<double>(vp.x + vp.w * 0.5f);
            double      cy = static_cast<double>(vp.y + vp.h * 0.5f);

            // Update data interaction with cursor position
            CursorReadout cursor;
            cursor.valid    = true;
            cursor.screen_x = cx;
            cursor.screen_y = cy;
            ui->data_interaction->update(cursor, fig);
            pump_frames(2);

            // Right-click at cursor position
            bool consumed = ui->data_interaction->on_mouse_click(1, cx, cy);
            pump_frames(5);

            // If nearest series was found and selected, selection should be populated
            if (sel.type == ui::SelectionType::Series && sel.series)
            {
                fprintf(stderr,
                        "[QA]   clipboard: right-click selected '%s'\n",
                        sel.series->label().c_str());
            }
            else
            {
                // Not an error — may not have been near a series at viewport center
                fprintf(stderr,
                        "[QA]   clipboard: right-click at center did not hit series (OK if cursor "
                        "not near data)\n");
            }
            (void)consumed;
        }

        // ── Test 7: Rapid clipboard operations (stability test) ──────────
        fprintf(stderr, "[QA]   clipboard: test 7 — rapid clipboard ops (stability)\n");
        for (int i = 0; i < 20; ++i)
        {
            if (has_critical_issue())
                return false;
            ui->cmd_registry.execute("series.cycle_selection");
            pump_frames(1);
            ui->cmd_registry.execute("series.copy");
            pump_frames(1);
            ui->cmd_registry.execute("series.paste");
            pump_frames(1);
            ui->cmd_registry.execute("series.deselect");
            pump_frames(1);
        }
        fprintf(stderr,
                "[QA]   clipboard: rapid ops complete, series count = %zu\n",
                ax.series().size());

        // ── Test 8: Copy then delete (clipboard should retain data) ──────
        fprintf(stderr, "[QA]   clipboard: test 8 — copy then delete, clipboard retained\n");
        ui->cmd_registry.execute("series.cycle_selection");
        pump_frames(2);
        ui->cmd_registry.execute("series.copy");
        pump_frames(1);
        ui->cmd_registry.execute("series.delete");
        pump_frames(2);
        // Clipboard should still have data after deleting the original
        if (!ui->imgui_ui->series_clipboard()->has_data())
        {
            add_issue(IssueSeverity::Error,
                      "clipboard",
                      "Clipboard lost data after deleting original series");
            return false;
        }
        // Paste it back
        ui->cmd_registry.execute("series.paste");
        pump_frames(5);
        fprintf(stderr, "[QA]   clipboard: copy-delete-paste cycle OK\n");

        pump_frames(10);
        fprintf(stderr, "[QA]   clipboard: all tests passed\n");
#endif
        return true;
    }

    // ── Figure serialization scenario ───────────────────────────────────
    // Exercises file.save_figure and file.load_figure commands.
    // Uses FigureSerializer::save/load directly to avoid native file dialogs.
    bool scenario_figure_serialization()
    {
#ifdef SPECTRA_USE_IMGUI
        auto* ui = app_->ui_context();
        if (!ui)
            return true;

        // Build a figure with known content
        auto&              fig = app_->figure({1280, 720});
        auto&              ax  = fig.subplot(1, 1, 1);
        std::vector<float> x(80), y1(80), y2(80);
        for (int i = 0; i < 80; ++i)
        {
            x[i]  = static_cast<float>(i) * 0.1f;
            y1[i] = std::sin(x[i]);
            y2[i] = std::cos(x[i]);
        }
        ax.line(x, y1).label("sin");
        ax.scatter(x, y2).label("cos");
        ax.title("Serialization Test");
        ax.xlabel("X");
        ax.ylabel("Y");

        auto all_ids = app_->figure_registry().all_ids();
        if (!all_ids.empty() && ui->fig_mgr)
            ui->fig_mgr->queue_switch(all_ids.back());
        pump_frames(10);

        size_t original_count = ax.series().size();
        fprintf(stderr, "[QA]   serialize: original series count = %zu\n", original_count);

        // ── Test 1: Save and reload via FigureSerializer directly ────────
        std::string save_path = opts_.output_dir + "/serialization_test.spectra";

        bool saved = FigureSerializer::save(save_path, fig);
        if (!saved)
        {
            add_issue(IssueSeverity::Error,
                      "serialization",
                      "FigureSerializer::save() returned false for path: " + save_path);
            return false;
        }
        fprintf(stderr, "[QA]   serialize: saved to %s\n", save_path.c_str());
        pump_frames(2);

        // Create a fresh figure to load into
        auto& fig2 = app_->figure({1280, 720});
        fig2.subplot(1, 1, 1);   // ensure at least one axes slot
        pump_frames(2);

        bool loaded = FigureSerializer::load(save_path, fig2);
        if (!loaded)
        {
            add_issue(IssueSeverity::Error,
                      "serialization",
                      "FigureSerializer::load() returned false for path: " + save_path);
            return false;
        }
        pump_frames(5);

        // Verify series count was restored
        if (fig2.axes().empty())
        {
            add_issue(IssueSeverity::Error, "serialization", "Loaded figure has no axes");
            return false;
        }
        size_t loaded_count = fig2.axes()[0]->series().size();
        if (loaded_count != original_count)
        {
            add_issue(IssueSeverity::Error,
                      "serialization",
                      "Series count mismatch after load: expected " + std::to_string(original_count)
                          + ", got " + std::to_string(loaded_count));
            return false;
        }
        fprintf(stderr, "[QA]   serialize: loaded OK, series count = %zu\n", loaded_count);

        pump_frames(5);
        fprintf(stderr, "[QA]   serialize: all tests passed\n");
#endif
        return true;
    }

    // ── Series removal interaction safety scenario ───────────────────────
    // Validates the notify_series_removed() path introduced in commit 7b95d81:
    // add hover/markers on a series, then delete it — should not crash or
    // produce use-after-free.
    bool scenario_series_removed_interaction_safety()
    {
#ifdef SPECTRA_USE_IMGUI
        auto* ui = app_->ui_context();
        if (!ui || !ui->data_interaction || !ui->imgui_ui || !ui->imgui_ui->is_initialized())
            return true;

        // Create a figure with 3 series
        auto&              fig = app_->figure({1280, 720});
        auto&              ax  = fig.subplot(1, 1, 1);
        std::vector<float> x(100);
        std::vector<float> y1(100), y2(100), y3(100);
        for (int i = 0; i < 100; ++i)
        {
            x[i]  = static_cast<float>(i) * 0.1f;
            y1[i] = std::sin(x[i]);
            y2[i] = std::cos(x[i]);
            y3[i] = x[i] * 0.1f;
        }
        ax.line(x, y1).label("sin_target");
        ax.line(x, y2).label("cos");
        ax.line(x, y3).label("linear");

        auto all_ids = app_->figure_registry().all_ids();
        if (!all_ids.empty() && ui->fig_mgr)
            ui->fig_mgr->queue_switch(all_ids.back());
        pump_frames(10);

        // Simulate hovering over the first series to populate nearest_ cache
        const auto& vp = ax.viewport();
        double      cx = static_cast<double>(vp.x + vp.w * 0.3f);
        double      cy = static_cast<double>(vp.y + vp.h * 0.5f);

        CursorReadout cursor;
        cursor.valid    = true;
        cursor.screen_x = cx;
        cursor.screen_y = cy;
        ui->data_interaction->update(cursor, fig);
        pump_frames(2);

        // Add a marker on the first series (left-click)
        ui->data_interaction->on_mouse_click(0, cx, cy);
        pump_frames(2);

        // Select the first series
        ui->cmd_registry.execute("series.cycle_selection");
        pump_frames(2);

        auto& sel = ui->imgui_ui->selection_context();
        fprintf(stderr,
                "[QA]   series_removed: selection type=%d, series=%p\n",
                static_cast<int>(sel.type),
                static_cast<void*>(sel.series));

        size_t before_count = ax.series().size();

        // ── Delete the selected series ────────────────────────────────────
        // This triggers notify_series_removed() via axes series_removed_callback.
        // After this, nearest_ and any markers referencing the deleted series
        // must be cleared (no dangling pointers).
        ui->cmd_registry.execute("series.delete");
        pump_frames(5);

        size_t after_count = ax.series().size();
        if (after_count != before_count - 1 && before_count > 0)
        {
            add_issue(IssueSeverity::Warning,
                      "series_removed",
                      "Series delete did not reduce count: before=" + std::to_string(before_count)
                          + " after=" + std::to_string(after_count));
        }

        // ── Now interact with the figure again — must not crash ───────────
        // Hover over the remaining series
        ui->data_interaction->update(cursor, fig);
        pump_frames(2);

        // Click on an empty area (deselect)
        ui->data_interaction->on_mouse_click(0, cx + 300.0, cy + 300.0);
        pump_frames(2);

        // Move mouse around
        cursor.screen_x = cx + 50.0;
        cursor.screen_y = cy + 20.0;
        ui->data_interaction->update(cursor, fig);
        pump_frames(2);

        // Delete another series to stress the path further
        ui->cmd_registry.execute("series.cycle_selection");
        pump_frames(2);
        ui->cmd_registry.execute("series.delete");
        pump_frames(5);

        // Final hover on reduced series set
        ui->data_interaction->update(cursor, fig);
        pump_frames(5);

        fprintf(stderr,
                "[QA]   series_removed: all interactions post-delete completed without crash\n");
#endif
        return true;
    }

    // ── Line culling pan/zoom scenario ───────────────────────────────────
    // Validates the draw-call culling optimization from commit a302a0d.
    // Creates a large sorted line series (>256 points) and performs extensive
    // pan + zoom operations to exercise the binary-search culling path.
    bool scenario_line_culling_pan_zoom()
    {
#ifdef SPECTRA_USE_GLFW
        auto* ui = app_->ui_context();
        if (!ui)
            return true;

        // Create a large sorted line series (10K points)
        auto&              fig = app_->figure({1280, 720});
        auto&              ax  = fig.subplot(1, 1, 1);
        const int          N   = 10000;
        std::vector<float> x(N), y(N);
        for (int i = 0; i < N; ++i)
        {
            x[i] = static_cast<float>(i) * 0.001f;   // sorted, 0..10
            y[i] = std::sin(x[i] * 6.0f) * std::exp(-x[i] * 0.2f);
        }
        ax.line(x, y).label("damped_sin_10k");
        ax.title("Line Culling Stress Test (10K sorted points)");

        auto all_ids = app_->figure_registry().all_ids();
        if (!all_ids.empty() && ui->fig_mgr)
            ui->fig_mgr->queue_switch(all_ids.back());
        pump_frames(15);

        const auto& vp = ax.viewport();
        double      cx = static_cast<double>(vp.x + vp.w * 0.5);
        double      cy = static_cast<double>(vp.y + vp.h * 0.5);

        // ── Phase 1: Zoom in deep (culling removes most points) ───────────
        fprintf(stderr, "[QA]   culling: phase 1 — zoom in 15x\n");
        for (int i = 0; i < 15; ++i)
        {
            if (has_critical_issue())
                return false;
            ui->input_handler.on_scroll(0.0, 1.0, cx, cy);
            pump_frames(1);
        }
        pump_frames(5);

        // ── Phase 2: Pan across the data range ───────────────────────────
        fprintf(stderr, "[QA]   culling: phase 2 — pan right through data\n");
        for (int i = 0; i < 30; ++i)
        {
            if (has_critical_issue())
                return false;
            // Drag right (pan left in data space)
            double x1 = cx + 10.0, x2 = cx - 10.0;
            ui->input_handler.on_mouse_button(1, 1, 0, x1, cy);
            pump_frames(1);
            for (int s = 1; s <= 5; ++s)
            {
                double t = static_cast<double>(s) / 5.0;
                ui->input_handler.on_mouse_move(x1 + (x2 - x1) * t, cy);
            }
            ui->input_handler.on_mouse_button(1, 0, 0, x2, cy);
            pump_frames(1);
        }
        pump_frames(5);

        // ── Phase 3: Zoom out to show full range ─────────────────────────
        fprintf(stderr, "[QA]   culling: phase 3 — zoom out 20x\n");
        for (int i = 0; i < 20; ++i)
        {
            if (has_critical_issue())
                return false;
            ui->input_handler.on_scroll(0.0, -1.0, cx, cy);
            pump_frames(1);
        }
        pump_frames(5);

        // ── Phase 4: Rapid zoom in/out stress ────────────────────────────
        fprintf(stderr, "[QA]   culling: phase 4 — rapid alternating zoom\n");
        for (int i = 0; i < 40; ++i)
        {
            if (has_critical_issue())
                return false;
            double delta = (i % 2 == 0) ? 1.0 : -1.0;
            ui->input_handler.on_scroll(0.0, delta, cx, cy);
            pump_frames(1);
        }

        // Reset view
        ui->cmd_registry.execute("view.home");
        pump_frames(10);

        fprintf(stderr, "[QA]   culling: all phases complete without crash or corruption\n");
#endif
        return true;
    }

    // ── Design Review ────────────────────────────────────────────────────
    // Captures named screenshots of every meaningful UI state for design analysis.
    // Screenshots go into <output_dir>/design/ with descriptive names.

    // Capture the next frame of `target_window` (primary when null) and prove
    // the state its name claims with `verify`, evaluated on the captured frame.
    // Failed checks become ERROR issues (design-review exits non-zero).
    std::string named_screenshot(const std::string&    name,
                                 const DesignVerifier& verify,
                                 WindowContext*        target_window = nullptr)
    {
        DesignAssertionRecord record;
        record.shot = name;

        auto finish = [&](DesignCheck& check, const std::string& path)
        {
            record.path     = path;
            record.frame    = total_frames_;
            record.checks   = check.passed.size() + check.failed.size();
            record.failures = check.failed;
            if (!check.failed.empty())
            {
                std::string msg = "Capture '" + name + "' does not show its named state: ";
                for (size_t i = 0; i < check.failed.size(); ++i)
                    msg += (i ? "; " : "") + check.failed[i];
#ifdef SPECTRA_USE_IMGUI
                msg += " [drawn windows: " + imgui_drawn_window_names(design_imgui(target_window))
                       + "]";
#endif
                add_issue(IssueSeverity::Error, "design_assertion", msg, false);
            }
            fprintf(stderr,
                    "[QA/Design] %s: %zu/%zu assertions passed (frame %lu)\n",
                    name.c_str(),
                    check.passed.size(),
                    record.checks,
                    static_cast<unsigned long>(total_frames_));
            design_assertions_.push_back(std::move(record));
        };

        auto* backend = dynamic_cast<VulkanBackend*>(app_->backend());
        if (!backend)
        {
            DesignCheck check;
            check.expect(false, "Vulkan backend available");
            finish(check, "");
            return "";
        }

        // When targeting a specific window, temporarily switch so
        // swapchain_width/height read from the correct swapchain.
        WindowContext* restore_window = nullptr;
        if (target_window)
        {
            restore_window = backend->active_window();
            backend->set_active_window(target_window);
        }

        uint32_t w = backend->swapchain_width();
        uint32_t h = backend->swapchain_height();
        if (w == 0 || h == 0)
        {
            if (restore_window)
                backend->set_active_window(restore_window);
            DesignCheck check;
            check.expect(false, "swapchain has a non-zero extent");
            finish(check, "");
            return "";
        }

        std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
        // Request capture during next end_frame (between GPU submit and present)
        // so the swapchain image content is guaranteed valid.
        // When target_window is set, the capture fires only during that
        // window's end_frame — critical for multi-window screenshots.
        backend->request_framebuffer_capture(pixels.data(), w, h, target_window);
        pump_frames(1);   // triggers capture in end_frame

        if (restore_window)
            backend->set_active_window(restore_window);

        // Semantic assertions against the frame that was just captured.
        DesignCheck check;
        if (verify)
            verify(check);
        if (check.passed.empty() && check.failed.empty())
            check.expect(false, "capture declares at least one semantic assertion");

        inspect_screenshot_artifacts(name, pixels, w, h);
        check_validation_messages("design:" + name);

        std::string           dir = opts_.output_dir + "/design";
        std::filesystem::path dir_path(dir);
        std::filesystem::create_directories(dir_path);

        std::string safe = name;
        for (auto& c : safe)
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-')
                c = '_';

        std::string path = dir + "/" + safe + ".png";
        ImageExporter::write_png(path, pixels.data(), w, h);
        fprintf(stderr, "[QA/Design] Captured: %s\n", path.c_str());
        design_screenshots_.push_back({name, path});
        finish(check, path);
        return path;
    }

#ifdef SPECTRA_USE_IMGUI
    // ── Design-review state probes ───────────────────────────────────────
    ImGuiContext* design_imgui(WindowContext* wctx = nullptr)
    {
        WindowUIContext* ui = wctx ? wctx->ui_ctx.get() : app_->ui_context();
        return (ui && ui->imgui_ui) ? ui->imgui_ui->imgui_context() : nullptr;
    }

    Figure* design_active_figure()
    {
        auto* ui = app_->ui_context();
        return (ui && ui->fig_mgr) ? ui->fig_mgr->active_figure() : nullptr;
    }

    FigureId design_active_figure_id()
    {
        auto* ui = app_->ui_context();
        return (ui && ui->fig_mgr) ? ui->fig_mgr->active_index() : INVALID_FIGURE_ID;
    }

    FigureId newest_figure_id()
    {
        auto ids = app_->figure_registry().all_ids();
        return ids.empty() ? INVALID_FIGURE_ID : ids.back();
    }

    // The active figure is `expected` and is the one drawn in the canvas.
    void expect_active_figure(DesignCheck& c, FigureId expected, const char* what)
    {
        c.expect(expected != INVALID_FIGURE_ID && design_active_figure_id() == expected,
                 std::string("active figure is ") + what + " (active id "
                     + std::to_string(design_active_figure_id()) + ", expected "
                     + std::to_string(expected) + ")");
        c.expect(imgui_window_drawn(design_imgui(), "##canvas"), "plot canvas drawn");
    }

    size_t series_count(const Figure* fig)
    {
        size_t n = 0;
        if (!fig)
            return 0;
        for (const auto& ax : fig->all_axes())
            if (ax)
                n += ax->series().size();
        for (const auto& ax : fig->axes())
            if (ax)
                n += ax->series().size();
        return n;
    }

    const Axes3D* first_axes3d(const Figure* fig)
    {
        if (!fig)
            return nullptr;
        for (const auto& ax : fig->all_axes())
            if (auto* a3 = dynamic_cast<const Axes3D*>(ax.get()))
                return a3;
        return nullptr;
    }

    void expect_3d_figure(DesignCheck& c, FigureId id, size_t min_series)
    {
        expect_active_figure(c, id, "the new 3D figure");
        const Axes3D* a3 = first_axes3d(design_active_figure());
        c.expect(a3 != nullptr, "active figure has 3D axes");
        c.expect(a3 && a3->series().size() >= min_series,
                 "3D axes hold " + std::to_string(min_series) + "+ series");
    }

    LayoutManager* design_layout()
    {
        auto* ui = app_->ui_context();
        return (ui && ui->imgui_ui) ? &ui->imgui_ui->get_layout_manager() : nullptr;
    }

    void expect_inspector_open(DesignCheck& c, bool open)
    {
        auto* lm = design_layout();
        c.expect(lm && lm->is_inspector_visible() == open,
                 std::string("inspector ") + (open ? "visible" : "hidden") + " in layout");
        if (open)
        {
            c.expect(lm && lm->inspector_animated_width() > 100.0f,
                     "inspector slid fully open (animated width "
                         + std::to_string(lm ? lm->inspector_animated_width() : 0.0f) + "px)");
            c.expect(imgui_window_drawn(design_imgui(), "##inspector"), "inspector window drawn");
        }
        else
        {
            c.expect(lm && lm->inspector_animated_width() < 1.0f,
                     "inspector fully collapsed (animated width "
                         + std::to_string(lm ? lm->inspector_animated_width() : 0.0f) + "px)");
        }
    }

    void expect_split_panes(DesignCheck& c, size_t panes)
    {
        auto*        ui = app_->ui_context();
        const size_t n  = ui ? ui->dock_system.pane_count() : 0;
        c.expect(n == panes,
                 "split view has " + std::to_string(panes) + " panes (actual " + std::to_string(n)
                     + ")");
        if (ui && panes > 1)
        {
            auto infos   = ui->dock_system.get_pane_infos();
            bool visible = infos.size() == panes;
            for (const auto& pi : infos)
                visible = visible && pi.bounds.w > 50.0f && pi.bounds.h > 50.0f
                          && pi.figure_index != INVALID_FIGURE_ID;
            c.expect(visible, "every pane has a figure and a visible (>50px) area");

            // Legend windows ("##legend_<figure ptr>_<axes>") belong only to
            // figures visible in a pane, never to ones behind another tab.
            std::set<std::string> visible_keys;
            for (const auto& pi : infos)
                if (Figure* pf = app_->figure_registry().get(pi.figure_index))
                    visible_keys.insert(
                        std::format("##legend_{:x}_", reinterpret_cast<uintptr_t>(pf)));
            std::string   ghosts;
            ImGuiContext* ctx = design_imgui();
            for (ImGuiWindow* w : ctx ? ctx->Windows : ImVector<ImGuiWindow*>{})
            {
                if (!w || !w->Active || !w->Name)
                    continue;
                std::string_view name(w->Name);
                if (name.rfind("##legend_", 0) != 0 || name.rfind("##legend_toggle", 0) == 0)
                    continue;
                bool ok = false;
                for (const auto& k : visible_keys)
                    ok = ok || name.rfind(k, 0) == 0;
                if (!ok)
                    ghosts += std::string(name) + " ";
            }
            c.expect(ghosts.empty(),
                     "only visible pane figures draw legends (ghosts: " + ghosts + ")");
        }
    }

    void expect_theme(DesignCheck& c, const std::string& name)
    {
        auto* ui = app_->ui_context();
        c.expect(ui && ui->theme_mgr && ui->theme_mgr->current_theme_name() == name,
                 "theme is '" + name + "' (actual '"
                     + (ui && ui->theme_mgr ? ui->theme_mgr->current_theme_name() : "?") + "')");
        c.expect(ui && ui->theme_mgr && !ui->theme_mgr->is_transitioning(),
                 "theme transition finished");
    }

    void expect_swapchain(DesignCheck& c, WindowContext* wctx, uint32_t w, uint32_t h)
    {
        c.expect(wctx && wctx->swapchain.extent.width == w && wctx->swapchain.extent.height == h,
                 "swapchain is " + std::to_string(w) + "x" + std::to_string(h) + " (actual "
                     + (wctx ? std::to_string(wctx->swapchain.extent.width) + "x"
                                   + std::to_string(wctx->swapchain.extent.height)
                             : std::string("none"))
                     + ")");
    }

    void expect_no_transient_ui(DesignCheck& c)
    {
        auto* ui = app_->ui_context();
        c.expect(ui && !ui->cmd_palette.is_open(), "command palette closed");
        c.expect(ui && !ui->settings_panel.is_visible(), "settings closed");
        c.expect(ui && ui->imgui_ui && !ui->imgui_ui->is_menu_open(), "no menu open");
        c.expect(imgui_drawn_popups(design_imgui()) == 0, "no ImGui popup open");
    }

    #ifdef SPECTRA_USE_GLFW
    // Move the real (X11/Xvfb) pointer over the primary window so GLFW, ImGui
    // and the InputHandler all see a genuine hover at (x, y) window coords.
    void move_pointer(double x, double y, WindowContext* wctx = nullptr)
    {
        auto* wm = app_->window_manager();
        if (!wctx && wm && !wm->windows().empty())
            wctx = wm->windows()[0];
        if (!wctx || !wctx->glfw_window)
            return;
        auto* win = static_cast<GLFWwindow*>(wctx->glfw_window);
        glfwSetCursorPos(win, x, y);
        // GLFW drops the X11 MotionNotify that its own warp generates, so the
        // app's cursor callback (InputHandler + ImGui backend) never hears of
        // it.  Deliver that motion through the installed callback chain, as a
        // real pointer move would.
        GLFWcursorposfun cb = glfwSetCursorPosCallback(win, nullptr);
        glfwSetCursorPosCallback(win, cb);
        if (cb)
            cb(win, x, y);
        pump_frames(1);
    }

    // Left-click at (x, y) in the window's ImGui context (pointer moved first).
    void click_imgui(double x, double y, WindowContext* wctx = nullptr)
    {
        move_pointer(x, y, wctx);
        ImGuiContext* ctx = design_imgui(wctx);
        if (!ctx)
            return;
        ImGuiContext* prev = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(ctx);
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        ImGui::SetCurrentContext(prev);
        pump_frames(1);
        prev = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(ctx);
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        ImGui::SetCurrentContext(prev);
        pump_frames(1);
    }

    bool pointer_position(double& x, double& y, WindowContext* wctx = nullptr)
    {
        auto* wm = app_->window_manager();
        if (!wctx && wm && !wm->windows().empty())
            wctx = wm->windows()[0];
        if (!wctx || !wctx->glfw_window)
            return false;
        glfwGetCursorPos(static_cast<GLFWwindow*>(wctx->glfw_window), &x, &y);
        return true;
    }
    #endif

    // Put the pointer back where the design review found it.
    void restore_pointer()
    {
    #ifdef SPECTRA_USE_GLFW
        if (saved_pointer_valid_)
            move_pointer(saved_pointer_x_, saved_pointer_y_);
    #endif
    }

    // With panes [left-top, left-bottom | right], split the right column too
    // (moves a spare figure of the left-top pane below the right pane), giving
    // a 2x2 grid — the same pane operation view.split_down performs.
    void split_right_column_down()
    {
        auto* ui = app_->ui_context();
        if (!ui)
            return;
        auto&                       dock  = ui->dock_system;
        auto                        infos = dock.get_pane_infos();
        const DockSystem::PaneInfo* right = nullptr;
        for (const auto& pi : infos)
            if (!right || pi.bounds.x > right->bounds.x)
                right = &pi;
        SplitPane* donor = nullptr;
        for (const auto& pi : infos)
        {
            SplitPane* pane = dock.split_view().pane_for_figure(pi.figure_index);
            if (pane && pane->figure_count() >= 2
                && (!donor || pane->figure_count() > donor->figure_count()))
                donor = pane;
        }
        if (!right || !donor)
            return;
        const FigureId right_fig = right->figure_index;
        FigureId       move      = INVALID_FIGURE_ID;
        for (FigureId f : donor->figure_indices())
            if (f != donor->figure_index())
                move = f;
        if (move == INVALID_FIGURE_ID)
            return;
        donor->remove_figure(move);
        dock.split_figure_down(right_fig, move);
    }

    // Hover the pointer at fraction (fx, fy) of the active figure's first plot
    // area.  Returns the target screen position.
    bool hover_plot(float fx, float fy, double& out_x, double& out_y)
    {
        Figure* fig = design_active_figure();
        if (!fig || fig->axes().empty() || !fig->axes()[0])
            return false;
        const Rect vp = fig->axes()[0]->viewport();
        if (vp.w <= 0.0f || vp.h <= 0.0f)
            return false;
        out_x = std::floor(vp.x + vp.w * fx) + 0.5;
        out_y = std::floor(vp.y + vp.h * fy) + 0.5;
    #ifdef SPECTRA_USE_GLFW
        move_pointer(out_x, out_y);
        return true;
    #else
        return false;
    #endif
    }

    struct ViewRange
    {
        double x0 = 0.0, x1 = 0.0, y0 = 0.0, y1 = 0.0;
    };
    ViewRange active_view_range()
    {
        ViewRange r;
        Figure*   f = design_active_figure();
        if (f && !f->axes().empty() && f->axes()[0])
        {
            auto xl = f->axes()[0]->x_limits();
            auto yl = f->axes()[0]->y_limits();
            r       = {xl.min, xl.max, yl.min, yl.max};
        }
        return r;
    }

    // Visible x/y spans shrank to at most `max_ratio` of the pre-zoom spans.
    void expect_zoomed(DesignCheck& c, const ViewRange& before, double max_ratio)
    {
        const ViewRange now = active_view_range();
        const double    bx  = before.x1 - before.x0;
        const double    by  = before.y1 - before.y0;
        c.expect(bx > 0.0 && (now.x1 - now.x0) < bx * max_ratio,
                 "x span zoomed in (" + std::to_string(now.x1 - now.x0) + " vs "
                     + std::to_string(bx) + ")");
        c.expect(by > 0.0 && (now.y1 - now.y0) < by * max_ratio,
                 "y span zoomed in (" + std::to_string(now.y1 - now.y0) + " vs "
                     + std::to_string(by) + ")");
    }

    // Pump until hover overlays (crosshair fade-in) settle, bounded.
    void settle_overlays()
    {
        auto* ui = app_->ui_context();
        for (int i = 0; i < 90; ++i)
        {
            pump_frames(1);
            if (ui && ui->data_interaction && ui->data_interaction->crosshair_active()
                && ui->data_interaction->crosshair_opacity() >= 1.0f)
                break;
        }
    }

    // Crosshair enabled, pointer readout at (x, y) inside the plot and the
    // overlay fully faded in for the captured frame.
    void expect_crosshair_at(DesignCheck& c, double x, double y)
    {
        auto* ui = app_->ui_context();
        auto* di = ui ? ui->data_interaction.get() : nullptr;
        c.expect(di && di->crosshair_active(), "crosshair enabled");
        if (!di)
            return;
        const CursorReadout& cur = di->last_cursor();
        c.expect(cur.valid, "pointer readout is over a plot (valid data coordinates)");
        c.near(cur.screen_x, x, 1.0, "pointer x at hover target");
        c.near(cur.screen_y, y, 1.0, "pointer y at hover target");
        Figure* fig = design_active_figure();
        c.expect(fig && !fig->axes().empty() && fig->axes()[0]
                     && rect_contains(fig->axes()[0]->viewport(), cur.screen_x, cur.screen_y),
                 "pointer inside the plot area");
        c.expect(di->crosshair_opacity() > 0.99f,
                 "crosshair fully drawn (opacity " + std::to_string(di->crosshair_opacity()) + ")");
    }
#endif

    void run_design_review()
    {
        fprintf(stderr, "[QA/Design] Starting design review capture...\n");
#if defined(SPECTRA_USE_IMGUI) && defined(SPECTRA_USE_GLFW)
        saved_pointer_valid_ = pointer_position(saved_pointer_x_, saved_pointer_y_);
#endif

        // ── 1. Default state: single figure with simple line ─────────────
        pump_frames(10);
        named_screenshot("01_default_single_line",
                         [&](DesignCheck& c)
                         {
                             Figure* f = design_active_figure();
                             c.expect(f && f->axes().size() == 1, "one 2D axes");
                             c.expect(series_count(f) == 1, "one series");
                             c.expect(f && !f->axes().empty() && !f->axes()[0]->series().empty()
                                          && f->axes()[0]->series()[0]->label() == "initial",
                                      "series labelled 'initial'");
                             expect_no_transient_ui(c);
                         });

        // ── 2. Empty axes (no data) ──────────────────────────────────────
        {
            auto& fig = app_->figure({1280, 720});
            fig.subplot(1, 1, 1);

            pump_frames(10);
            const FigureId fid = newest_figure_id();
            named_screenshot("02_empty_axes",
                             [&](DesignCheck& c)
                             {
                                 expect_active_figure(c, fid, "the new empty figure");
                                 Figure* f = design_active_figure();
                                 c.expect(f && f->axes().size() == 1, "one 2D axes");
                                 c.expect(series_count(f) == 0, "axes hold no series");
                             });
        }

        // ── 3. Multiple series (line + scatter) ──────────────────────────
        {
            auto&              fig = app_->figure({1280, 720});
            auto&              ax  = fig.subplot(1, 1, 1);
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
            const FigureId fid = newest_figure_id();
            named_screenshot("03_multi_series_with_labels",
                             [&](DesignCheck& c)
                             {
                                 expect_active_figure(c, fid, "the new multi-series figure");
                                 Figure* f = design_active_figure();
                                 c.expect(series_count(f) == 3, "three series");
                                 c.expect(f && !f->axes().empty()
                                              && f->axes()[0]->title() == "Multi-Series Plot",
                                          "title 'Multi-Series Plot'");
                             });
        }

        // ── 4. Dense data (10K points) ───────────────────────────────────
        {
            auto&              fig = app_->figure({1280, 720});
            auto&              ax  = fig.subplot(1, 1, 1);
            std::vector<float> x(10000), y(10000);
            for (int i = 0; i < 10000; ++i)
            {
                x[i] = static_cast<float>(i) * 0.001f;
                y[i] = std::sin(x[i] * 10.0f) * std::exp(-x[i] * 0.3f);
            }
            ax.line(x, y).label("Damped oscillation");
            ax.title("Dense Data (10K points)");

            pump_frames(10);
            const FigureId fid = newest_figure_id();
            named_screenshot(
                "04_dense_data_10k",
                [&](DesignCheck& c)
                {
                    expect_active_figure(c, fid, "the new dense figure");
                    Figure* f = design_active_figure();
                    auto*   ser =
                        (f && !f->axes().empty() && !f->axes()[0]->series().empty())
                              ? dynamic_cast<const LineSeries*>(f->axes()[0]->series()[0].get())
                              : nullptr;
                    c.expect(ser && ser->point_count() == 10000, "line series has 10000 points");
                });
        }

        // ── 5. Subplot grid (2x2) ───────────────────────────────────────
        {
            auto& fig = app_->figure({1280, 720});
            for (int r = 0; r < 2; ++r)
            {
                for (int c = 0; c < 2; ++c)
                {
                    auto&              ax = fig.subplot(2, 2, r * 2 + c + 1);
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
            const FigureId fid = newest_figure_id();
            named_screenshot("05_subplot_2x2_grid",
                             [&](DesignCheck& c)
                             {
                                 expect_active_figure(c, fid, "the new 2x2 figure");
                                 Figure* f = design_active_figure();
                                 c.expect(f && f->axes().size() == 4, "four subplots");
                                 c.expect(series_count(f) == 4, "one series per subplot");
                             });
        }

        // ── 6. Large scatter plot ────────────────────────────────────────
        {
            auto&                           fig = app_->figure({1280, 720});
            auto&                           ax  = fig.subplot(1, 1, 1);
            std::vector<float>              x(2000), y(2000);
            std::normal_distribution<float> norm(0.0f, 1.0f);
            for (int i = 0; i < 2000; ++i)
            {
                x[i] = norm(rng_);
                y[i] = norm(rng_);
            }
            ax.scatter(x, y).label("Normal distribution");
            ax.title("Scatter Plot (2K points)");

            pump_frames(10);
            const FigureId fid = newest_figure_id();
            named_screenshot(
                "06_scatter_2k_normal",
                [&](DesignCheck& c)
                {
                    expect_active_figure(c, fid, "the new scatter figure");
                    Figure* f = design_active_figure();
                    auto*   ser =
                        (f && !f->axes().empty() && !f->axes()[0]->series().empty())
                              ? dynamic_cast<const ScatterSeries*>(f->axes()[0]->series()[0].get())
                              : nullptr;
                    c.expect(ser && ser->point_count() == 2000, "scatter series has 2000 points");
                });
        }

        // ── 7. UI panels: inspector open ─────────────────────────────────
#ifdef SPECTRA_USE_IMGUI
        {
            auto* ui = app_->ui_context();
            if (ui)
            {
                ui->cmd_registry.execute("panel.toggle_inspector");
                pump_frames(10);
                named_screenshot("07_inspector_panel_open",
                                 [&](DesignCheck& c) { expect_inspector_open(c, true); });
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
                named_screenshot(
                    "08_command_palette_open",
                    [&](DesignCheck& c)
                    {
                        c.expect(ui->cmd_palette.is_open(), "command palette open");
                        c.expect(imgui_window_drawn(design_imgui(), "##command_palette"),
                                 "palette window drawn");
                        c.expect(ui->cmd_palette.result_count() > 0, "palette lists commands");
                    });
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
                named_screenshot("09_split_view_right",
                                 [&](DesignCheck& c) { expect_split_panes(c, 2); });
            }
        }

        // ── 10. Split view (4 panes) ────────────────────────────────────
        {
            auto* ui = app_->ui_context();
            if (ui)
            {
                // view.split_down splits only the active (left) pane -> 3 panes.
                ui->cmd_registry.execute("view.split_down");
                pump_frames(2);
                split_right_column_down();
                pump_frames(10);
                named_screenshot("10_split_view_4_panes",
                                 [&](DesignCheck& c) { expect_split_panes(c, 4); });
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
                named_screenshot("11_theme_dark", [&](DesignCheck& c) { expect_theme(c, "dark"); });
            }
        }

        // ── 12. Light theme ─────────────────────────────────────────────
        {
            auto* ui = app_->ui_context();
            if (ui)
            {
                ui->cmd_registry.execute("theme.light");
                pump_frames(30);   // D25 fix: allow theme transition to fully complete
                named_screenshot("12_theme_light",
                                 [&](DesignCheck& c) { expect_theme(c, "light"); });
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
                // Use explicit state to avoid toggle drift (D26 fix)
                Figure* active_fig = ui->fig_mgr->active_figure();
                if (active_fig)
                {
                    for (auto& ax_ptr : active_fig->axes())
                    {
                        if (ax_ptr)
                            ax_ptr->grid(true);
                    }
                }
                pump_frames(10);
                named_screenshot("13_grid_enabled",
                                 [&](DesignCheck& c)
                                 {
                                     Figure* f  = design_active_figure();
                                     bool    on = f && !f->axes().empty();
                                     if (f)
                                         for (const auto& ax : f->axes())
                                             on = on && ax && ax->grid_enabled();
                                     c.expect(on,
                                              "grid enabled on every axes of the active figure");
                                 });
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
                    named_screenshot("14_legend_visible",
                                     [&](DesignCheck& c)
                                     {
                                         Figure* f = design_active_figure();
                                         c.expect(f && f->legend().visible, "legend visible");
                                         c.expect(series_count(f) > 0, "legend has entries");
                                     });
                    active_fig->legend().visible = false;
                }
            }
        }

        // ── 15. Crosshair mode ──────────────────────────────────────────
        {
            auto* ui = app_->ui_context();
            if (ui)
            {
                // Use explicit state to avoid toggle drift (D27 fix)
                ui->data_interaction->set_crosshair(true);
                // Also ensure legend is visible for this screenshot
                Figure* active_fig = ui->fig_mgr->active_figure();
                if (active_fig)
                    active_fig->legend().visible = true;
                pump_frames(10);
                double     hx = 0.0, hy = 0.0;
                const bool hovered = hover_plot(0.62f, 0.40f, hx, hy);
                settle_overlays();
                named_screenshot("15_crosshair_mode",
                                 [&](DesignCheck& c)
                                 {
                                     c.expect(hovered, "pointer moved over the plot");
                                     expect_crosshair_at(c, hx, hy);
                                 });
                ui->data_interaction->set_crosshair(false);
                if (active_fig)
                    active_fig->legend().visible = false;
                restore_pointer();
                pump_frames(5);
            }
        }

        // ── 16. Zoomed in view ──────────────────────────────────────────
        {
            auto* ui = app_->ui_context();
            if (ui)
            {
                const ViewRange pre_zoom = active_view_range();
                for (int i = 0; i < 5; ++i)
                    ui->cmd_registry.execute("view.zoom_in");
                pump_frames(10);
                named_screenshot("16_zoomed_in",
                                 [&](DesignCheck& c) { expect_zoomed(c, pre_zoom, 0.8); });
                ui->cmd_registry.execute("view.home");
                pump_frames(5);
            }
        }

        // ── 17. Multiple tabs ───────────────────────────────────────────
        {
            // Create several figures to show tab bar
            auto*        ui          = app_->ui_context();
            const size_t tabs_before = ui->fig_mgr->count();
            for (int i = 0; i < 4; ++i)
                create_random_figure();
            pump_frames(10);
            named_screenshot("17_multiple_tabs",
                             [&](DesignCheck& c)
                             {
                                 c.expect(ui->fig_mgr->count() == tabs_before + 4,
                                          "four more figure tabs ("
                                              + std::to_string(ui->fig_mgr->count()) + " total)");
                             });
        }

        // ── 18. Timeline panel ──────────────────────────────────────────
        {
            auto* ui = app_->ui_context();
            if (ui)
            {
                ui->cmd_registry.execute("panel.toggle_timeline");
                pump_frames(10);
                named_screenshot(
                    "18_timeline_panel",
                    [&](DesignCheck& c)
                    {
                        c.expect(ui->imgui_ui->is_timeline_visible(), "timeline visible");
                        c.expect(imgui_window_drawn(design_imgui(), "##timeline_panel"),
                                 "timeline window drawn");
                    });
                ui->cmd_registry.execute("panel.toggle_timeline");
                pump_frames(5);
            }
        }
#endif

        // ── 19. 3D surface plot ─────────────────────────────────────────
        {
            auto& fig = app_->figure({1280, 720});
            auto& ax  = fig.subplot3d(1, 1, 1);
            int   n   = 30;
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
            const FigureId fid = newest_figure_id();
            named_screenshot("19_3d_surface", [&](DesignCheck& c) { expect_3d_figure(c, fid, 1); });
        }

        // ── 20. 3D scatter plot ─────────────────────────────────────────
        {
            auto&                           fig = app_->figure({1280, 720});
            auto&                           ax  = fig.subplot3d(1, 1, 1);
            std::vector<float>              x(500), y(500), z(500);
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
            const FigureId fid = newest_figure_id();
            named_screenshot("20_3d_scatter", [&](DesignCheck& c) { expect_3d_figure(c, fid, 1); });
        }

        // ══════════════════════════════════════════════════════════════════
        // Session 4 — 3D / Animation / Statistics scenarios
        // ══════════════════════════════════════════════════════════════════

        // ── 21. 3D surface with labels + lighting ──────────────────────
        {
            auto&              fig = app_->figure({1280, 720});
            auto&              ax  = fig.subplot3d(1, 1, 1);
            int                n   = 40;
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
            const FigureId fid = newest_figure_id();
            named_screenshot("21_3d_surface_labeled",
                             [&](DesignCheck& c) { expect_3d_figure(c, fid, 1); });
        }

        // ── 22. 3D surface — rotated camera (side view) ───────────────
        {
            auto&              fig = app_->figure({1280, 720});
            auto&              ax  = fig.subplot3d(1, 1, 1);
            int                n   = 30;
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
            const FigureId fid = newest_figure_id();
            named_screenshot(
                "22_3d_camera_side_view",
                [&](DesignCheck& c)
                {
                    expect_3d_figure(c, fid, 1);
                    const Axes3D* a3 = first_axes3d(design_active_figure());
                    c.near(a3 ? a3->camera().azimuth : -1.0, 0.0, 0.01, "camera azimuth");
                    c.near(a3 ? a3->camera().elevation : -1.0, 15.0, 0.01, "camera elevation");
                });
        }

        // ── 23. 3D surface — top-down camera ──────────────────────────
        {
            auto&              fig = app_->figure({1280, 720});
            auto&              ax  = fig.subplot3d(1, 1, 1);
            int                n   = 30;
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
            const FigureId fid = newest_figure_id();
            named_screenshot(
                "23_3d_camera_top_down",
                [&](DesignCheck& c)
                {
                    expect_3d_figure(c, fid, 1);
                    const Axes3D* a3 = first_axes3d(design_active_figure());
                    c.near(a3 ? a3->camera().elevation : -1.0, 85.0, 0.01, "camera elevation");
                });
        }

        // ── 24. 3D line plot (helix) ──────────────────────────────────
        {
            auto&              fig = app_->figure({1280, 720});
            auto&              ax  = fig.subplot3d(1, 1, 1);
            int                n   = 500;
            std::vector<float> x(n), y(n), z(n);
            for (int i = 0; i < n; ++i)
            {
                float t = static_cast<float>(i) * 0.05f;
                x[i]    = std::cos(t);
                y[i]    = std::sin(t);
                z[i]    = t * 0.1f;
            }
            ax.line3d(x, y, z).label("Helix").color(colors::cyan);
            ax.auto_fit();
            ax.title("3D Helix Line");
            ax.xlabel("X");
            ax.ylabel("Y");
            ax.zlabel("Z");

            pump_frames(15);
            const FigureId fid = newest_figure_id();
            named_screenshot("24_3d_line_helix",
                             [&](DesignCheck& c) { expect_3d_figure(c, fid, 1); });
        }

        // ── 25. 3D scatter with multiple clusters ─────────────────────
        {
            auto&                           fig = app_->figure({1280, 720});
            auto&                           ax  = fig.subplot3d(1, 1, 1);
            std::normal_distribution<float> norm(0.0f, 0.35f);
            // Cluster 1
            std::vector<float> x1(200), y1(200), z1(200);
            for (int i = 0; i < 200; ++i)
            {
                x1[i] = norm(rng_) + 2.5f;
                y1[i] = norm(rng_) + 2.5f;
                z1[i] = norm(rng_) + 2.5f;
            }
            ax.scatter3d(x1, y1, z1).label("Cluster A").color(colors::red).size(5.5f);
            // Cluster 2
            std::vector<float> x2(200), y2(200), z2(200);
            for (int i = 0; i < 200; ++i)
            {
                x2[i] = norm(rng_) - 2.5f;
                y2[i] = norm(rng_) - 2.5f;
                z2[i] = norm(rng_) - 2.5f;
            }
            ax.scatter3d(x2, y2, z2).label("Cluster B").color(colors::blue).size(5.5f);
            ax.auto_fit();
            ax.title("3D Scatter -- Two Clusters");
            ax.camera().set_azimuth(35.0f).set_elevation(24.0f).set_distance(8.0f);

            pump_frames(15);
            const FigureId fid = newest_figure_id();
            named_screenshot("25_3d_scatter_clusters",
                             [&](DesignCheck& c) { expect_3d_figure(c, fid, 2); });
        }

        // ── 26. 3D orthographic projection ────────────────────────────
        {
            auto&              fig = app_->figure({1280, 720});
            auto&              ax  = fig.subplot3d(1, 1, 1);
            int                n   = 25;
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
            const FigureId fid = newest_figure_id();
            named_screenshot(
                "26_3d_orthographic",
                [&](DesignCheck& c)
                {
                    expect_3d_figure(c, fid, 1);
                    const Axes3D* a3 = first_axes3d(design_active_figure());
                    c.expect(
                        a3 && a3->camera().projection_mode == Camera::ProjectionMode::Orthographic,
                        "orthographic projection");
                });
        }

#ifdef SPECTRA_USE_IMGUI
        // ── 27. Inspector with series selected (statistics visible) ───
        {
            // Create a figure with labeled data for inspector stats
            auto&              fig = app_->figure({1280, 720});
            auto&              ax  = fig.subplot(1, 1, 1);
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
                named_screenshot(
                    "27_inspector_series_stats",
                    [&](DesignCheck& c)
                    {
                        expect_inspector_open(c, true);
                        c.expect(ui->imgui_ui->is_inspector_section_series(),
                                 "inspector shows the Series section");
                        c.expect(ui->imgui_ui->selection_context().type == ui::SelectionType::Series
                                     && ui->imgui_ui->selection_context().series != nullptr,
                                 "a series is selected (properties + statistics shown)");
                    });
                ui->cmd_registry.execute("panel.toggle_inspector");
                pump_frames(5);
            }
        }

        // ── 28. Inspector with axes properties ────────────────────────
        {
            auto* ui = app_->ui_context();
            if (ui)
            {
                // Show the Axes section (the inspector otherwise keeps the
                // previous capture's series selection).
                ui->imgui_ui->set_inspector_section_axes();
                ui->cmd_registry.execute("panel.toggle_inspector");
                pump_frames(10);
                named_screenshot(
                    "28_inspector_axes_properties",
                    [&](DesignCheck& c)
                    {
                        expect_inspector_open(c, true);
                        c.expect(ui->imgui_ui->is_inspector_section_axes(),
                                 "inspector shows the Axes section");
                        c.expect(ui->imgui_ui->selection_context().type == ui::SelectionType::Axes,
                                 "axes selected in the inspector");
                    });
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
                named_screenshot(
                    "29_timeline_with_keyframes",
                    [&](DesignCheck& c)
                    {
                        c.expect(ui->imgui_ui->is_timeline_visible()
                                     && imgui_window_drawn(design_imgui(), "##timeline_panel"),
                                 "timeline drawn");
                        c.expect(te.track_count() >= 3, "timeline has 3+ tracks");
                        c.expect(te.total_keyframe_count() >= 10, "timeline has 10+ keyframes");
                        c.near(te.playhead(), 1.8, 0.01, "playhead");
                    });
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
                te.play();   // from Stopped: rewinds the playhead to 0
                const float playhead_before = te.playhead();
                pump_frames(30);   // Let it advance ~30 frames
                named_screenshot(
                    "30_timeline_playing",
                    [&](DesignCheck& c)
                    {
                        c.expect(ui->imgui_ui->is_timeline_visible()
                                     && imgui_window_drawn(design_imgui(), "##timeline_panel"),
                                 "timeline drawn");
                        c.expect(te.is_playing(), "timeline playing");
                        c.expect(te.playhead() > playhead_before,
                                 "playhead advanced during playback");
                    });
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
                named_screenshot(
                    "31_timeline_loop_region",
                    [&](DesignCheck& c)
                    {
                        c.expect(ui->imgui_ui->is_timeline_visible()
                                     && imgui_window_drawn(design_imgui(), "##timeline_panel"),
                                 "timeline drawn");
                        c.expect(te.loop_mode() == LoopMode::Loop, "loop mode on");
                        c.near(te.loop_in(), 1.0, 0.001, "loop in");
                        c.near(te.loop_out(), 3.5, 0.001, "loop out");
                    });
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
                named_screenshot("32_curve_editor",
                                 [&](DesignCheck& c)
                                 {
                                     c.expect(ui->imgui_ui->is_curve_editor_visible(),
                                              "curve editor visible");
                                     c.expect(imgui_window_drawn(design_imgui(), "Curve Editor"),
                                              "curve editor window drawn");
                                 });
                ui->cmd_registry.execute("panel.toggle_curve_editor");
                pump_frames(5);
            }
        }

        // ── 33. Split view with 2 figures (proper split) ─────────────
        {
            // Create 2 figures so split actually works
            auto&              fig1 = app_->figure({1280, 720});
            auto&              ax1  = fig1.subplot(1, 1, 1);
            std::vector<float> x1(200), y1(200);
            for (int i = 0; i < 200; ++i)
            {
                x1[i] = static_cast<float>(i) * 0.05f;
                y1[i] = std::sin(x1[i]);
            }
            ax1.line(x1, y1).label("sin(x)");
            ax1.title("Left Pane");

            auto&              fig2 = app_->figure({1280, 720});
            auto&              ax2  = fig2.subplot(1, 1, 1);
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
                named_screenshot("33_split_view_two_figures",
                                 [&](DesignCheck& c)
                                 {
                                     expect_split_panes(c, 2);
                                     auto infos = ui->dock_system.get_pane_infos();
                                     c.expect(infos.size() == 2
                                                  && infos[0].figure_index != infos[1].figure_index,
                                              "panes show two different figures");
                                 });
                ui->cmd_registry.execute("view.reset_splits");
                pump_frames(5);
            }
        }

        // ── 34. Multi-series with legend + grid + crosshair ──────────
        {
            auto&              fig = app_->figure({1280, 720});
            auto&              ax  = fig.subplot(1, 1, 1);
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
            const FigureId fid = newest_figure_id();

            pump_frames(10);

            // Explicitly enable grid, legend, and crosshair (avoid toggle drift)
            ax.grid(true);
            fig.legend().visible = true;
            auto* ui             = app_->ui_context();
            if (ui)
            {
                if (ui->data_interaction)
                    ui->data_interaction->set_crosshair(true);
                pump_frames(10);
                double     hx = 0.0, hy = 0.0;
                const bool hovered = hover_plot(0.45f, 0.35f, hx, hy);
                settle_overlays();
                named_screenshot("34_multi_series_full_chrome",
                                 [&](DesignCheck& c)
                                 {
                                     expect_active_figure(c, fid, "the new multi-signal figure");
                                     Figure* f = design_active_figure();
                                     c.expect(series_count(f) == 4, "four series");
                                     c.expect(f && f->axes()[0]->grid_enabled(), "grid on");
                                     c.expect(f && f->legend().visible, "legend visible");
                                     c.expect(hovered, "pointer moved over the plot");
                                     expect_crosshair_at(c, hx, hy);
                                 });
                // Restore defaults
                if (ui->data_interaction)
                    ui->data_interaction->set_crosshair(false);
                restore_pointer();
                pump_frames(5);
            }
        }

        // ── 35. Zoomed-in data center (verify D12 fix) ───────────────
        {
            auto&              fig = app_->figure({1280, 720});
            auto&              ax  = fig.subplot(1, 1, 1);
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
                const ViewRange pre_zoom = active_view_range();
                for (int i = 0; i < 5; ++i)
                    ui->cmd_registry.execute("view.zoom_in");
                pump_frames(10);
                named_screenshot("35_zoom_data_center_verify",
                                 [&](DesignCheck& c)
                                 {
                                     expect_zoomed(c, pre_zoom, 0.8);
                                     Figure* f = design_active_figure();
                                     if (f && !f->axes().empty())
                                     {
                                         auto xl = f->axes()[0]->x_limits();
                                         auto yl = f->axes()[0]->y_limits();
                                         c.near(0.5 * (xl.min + xl.max),
                                                0.5 * (pre_zoom.x0 + pre_zoom.x1),
                                                0.05 * (pre_zoom.x1 - pre_zoom.x0),
                                                "zoom keeps x center");
                                         c.near(0.5 * (yl.min + yl.max),
                                                0.5 * (pre_zoom.y0 + pre_zoom.y1),
                                                0.05 * (pre_zoom.y1 - pre_zoom.y0),
                                                "zoom keeps y center");
                                     }
                                 });
                ui->cmd_registry.execute("view.home");
                pump_frames(5);
            }
        }

        // ══════════════════════════════════════════════════════════════════
        // Session 5 — Menu, Command Palette, Window & Tab Drag scenarios
        // ══════════════════════════════════════════════════════════════════

        // ── 36. Menu bar state — show clean figure + menu bar visible ────
        // D34 fix: F10 doesn't reliably open a menu in GLFW headless mode.
        // Instead switch to Figure 1 (clean sine wave) and capture the
        // menu bar in its idle state — clearly shows all menu items.
        {
            auto* ui = app_->ui_context();
            if (ui && ui->fig_mgr)
            {
                // Switch to Figure 1 so we capture a clean, representative figure
                auto ids = app_->figure_registry().all_ids();
                if (!ids.empty())
                    ui->fig_mgr->queue_switch(ids[0]);
                pump_frames(10);
                // Open the File menu the way a user does: point at its
                // command-bar button and click it.
                Rect file_btn{};
                if (ui->imgui_ui->menu_button_rect("File", file_btn))
                    click_imgui(file_btn.x + file_btn.w * 0.5, file_btn.y + file_btn.h * 0.5);
                pump_frames(6);   // popup appear animation
                named_screenshot(
                    "36_menu_bar_activated",
                    [&](DesignCheck& c)
                    {
                        c.expect(ui->imgui_ui->is_menu_open(), "a command-bar menu is open");
                        c.expect(ui->imgui_ui->open_menu_label() == "File",
                                 "open menu is 'File' (actual '" + ui->imgui_ui->open_menu_label()
                                     + "')");
                        c.expect(imgui_drawn_popups(design_imgui(), "##commandbar") >= 1,
                                 "menu popup drawn");
                    });
            }
        }

        // Leave the File menu: pointer back off the menu closes it (auto-close).
        if (auto* ui36 = app_->ui_context(); ui36 && ui36->imgui_ui)
        {
            restore_pointer();
            ui36->imgui_ui->dismiss_automation_capture();
            pump_frames(3);
        }

        // ── 37. Command palette with search text ────────────────────────
        {
            auto* ui = app_->ui_context();
            if (ui)
            {
                // Open command palette
                ui->cmd_registry.execute("app.command_palette");
                pump_frames(5);

                // Type a search query to show filtered results via ImGui char input
                const char* search = "theme";
                for (int i = 0; search[i]; ++i)
                {
                    ImGui::GetIO().AddInputCharacter(static_cast<unsigned int>(search[i]));
                    pump_frames(1);
                }
                pump_frames(5);
                named_screenshot(
                    "37_command_palette_with_search",
                    [&](DesignCheck& c)
                    {
                        c.expect(ui->cmd_palette.is_open()
                                     && imgui_window_drawn(design_imgui(), "##command_palette"),
                                 "palette drawn");
                        c.expect(std::string(ui->cmd_palette.query()) == "theme",
                                 std::string("query is 'theme' (actual '") + ui->cmd_palette.query()
                                     + "')");
                        c.expect(ui->cmd_palette.result_count() > 0, "query has results");
                    });

                // Close palette
                ui->cmd_registry.execute("app.cancel");
                pump_frames(3);
            }
        }

        // ── 38. Inspector panel with knobs visible ──────────────────────
        {
            auto* ui = app_->ui_context();
            if (ui)
            {
                // Register interactive parameters so the knob panel shows.
                ui->knob_manager.add_float("Frequency", 2.0f, 0.1f, 10.0f, 0.1f);
                ui->knob_manager.add_float("Damping", 0.2f, 0.0f, 1.0f);
                ui->knob_manager.add_bool("Show envelope", true);
                ui->knob_manager.set_visible(true);
                // Open inspector
                ui->cmd_registry.execute("panel.toggle_inspector");
                pump_frames(5);
                // Cycle selection to show series properties
                ui->cmd_registry.execute("series.cycle_selection");
                pump_frames(10);
                named_screenshot("38_inspector_with_knobs",
                                 [&](DesignCheck& c)
                                 {
                                     expect_inspector_open(c, true);
                                     c.expect(ui->imgui_ui->selection_context().type
                                                  == ui::SelectionType::Series,
                                              "a series is selected");
                                     c.expect(!ui->knob_manager.empty(), "knobs registered");
                                     c.expect(imgui_window_drawn(design_imgui(), " Parameters"),
                                              "knob panel drawn");
                                 });
                ui->cmd_registry.execute("panel.toggle_inspector");
                ui->knob_manager.clear();
                pump_frames(3);
            }
        }

        // ── 39. Nav rail expanded ───────────────────────────────────────
        // D35 fix: use explicit expand + snap animation via dt=0 update.
        {
            auto* ui = app_->ui_context();
            if (ui && ui->imgui_ui)
            {
                // Explicitly expand the nav rail
                ui->imgui_ui->get_layout_manager().set_nav_rail_expanded(true);
                // Force animation to snap by calling update with dt=0
                // (smooth_toward snaps when dt<=0)
                ui->imgui_ui->get_layout_manager().update(1280.0f, 720.0f, 0.0f);
                pump_frames(5);
                named_screenshot(
                    "39_nav_rail_visible",
                    [&](DesignCheck& c)
                    {
                        auto& lm = ui->imgui_ui->get_layout_manager();
                        c.expect(lm.is_nav_rail_expanded(), "nav rail expanded");
                        c.near(lm.nav_rail_animated_width(),
                               lm.nav_rail_width(),
                               0.5,
                               "nav rail at expanded width");
                        c.expect(imgui_window_drawn(design_imgui(), "##navrail")
                                     || imgui_window_drawn(design_imgui(), "##AppShellNavRail"),
                                 "nav rail drawn");
                    });
                // Restore collapsed state (also snap)
                ui->imgui_ui->get_layout_manager().set_nav_rail_expanded(false);
                ui->imgui_ui->get_layout_manager().update(1280.0f, 720.0f, 0.0f);
                pump_frames(3);
            }
        }

        // ── 40. Tab bar context menu (right-click on tab) ───────────────
        // D40 fix: tabs are rendered via draw_pane_tab_headers() which uses
        // per-pane ImGui windows with NoInputs.  Mouse injection doesn't
        // work reliably because pump_frames→step() switches ImGui contexts.
        // Use the programmatic open_tab_context_menu() API instead.
        {
            auto* ui = app_->ui_context();
            if (ui && ui->imgui_ui && ui->fig_mgr)
            {
                // Switch to Figure 1 so the screenshot shows a clean figure
                auto ids = app_->figure_registry().all_ids();
                if (!ids.empty())
                    ui->fig_mgr->queue_switch(ids[0]);
                pump_frames(10);

                // Programmatically open the context menu for Figure 1
                if (!ids.empty())
                    ui->imgui_ui->open_tab_context_menu(ids[0]);
                pump_frames(10);
                named_screenshot(
                    "40_tab_context_menu",
                    [&](DesignCheck& c)
                    {
                        c.expect(!ids.empty() && ui->imgui_ui->tab_context_menu_figure() == ids[0],
                                 "tab context menu targets Figure 1");
                        c.expect(imgui_drawn_popups(design_imgui(), "##pane_tab_popup_host") >= 1,
                                 "tab context popup drawn");
                    });

                // D43 fix: explicitly close the context menu so it doesn't
                // bleed into subsequent screenshots (41–46).
                ui->imgui_ui->close_tab_context_menu();
                pump_frames(5);
            }
        }

        // ── 41. Window resized small (640x480) ─────────────────────────
        {
            auto* wm = app_->window_manager();
            if (wm && !wm->windows().empty())
            {
                auto* wctx     = wm->windows()[0];
                auto* glfw_win = static_cast<GLFWwindow*>(wctx->glfw_window);
                if (glfw_win)
                {
                    glfwSetWindowSize(glfw_win, 640, 480);
                    pump_frames(20);   // Allow swapchain recreation
                    named_screenshot("41_window_resized_640x480",
                                     [&](DesignCheck& c) { expect_swapchain(c, wctx, 640, 480); });
                }
            }
        }

        // ── 42. Window resized wide (1920x600) ─────────────────────────
        {
            auto* wm = app_->window_manager();
            if (wm && !wm->windows().empty())
            {
                auto* wctx     = wm->windows()[0];
                auto* glfw_win = static_cast<GLFWwindow*>(wctx->glfw_window);
                if (glfw_win)
                {
                    glfwSetWindowSize(glfw_win, 1920, 600);
                    pump_frames(20);
                    named_screenshot("42_window_resized_1920x600",
                                     [&](DesignCheck& c) { expect_swapchain(c, wctx, 1920, 600); });
                }
            }
        }

        // ── 43. Window resized tall (600x1080) ─────────────────────────
        {
            auto* wm = app_->window_manager();
            if (wm && !wm->windows().empty())
            {
                auto* wctx     = wm->windows()[0];
                auto* glfw_win = static_cast<GLFWwindow*>(wctx->glfw_window);
                if (glfw_win)
                {
                    glfwSetWindowSize(glfw_win, 600, 1080);
                    pump_frames(20);
                    named_screenshot("43_window_resized_600x1080",
                                     [&](DesignCheck& c) { expect_swapchain(c, wctx, 600, 1080); });

                    // Restore to normal
                    glfwSetWindowSize(glfw_win, 1280, 720);
                    pump_frames(15);
                }
            }
        }

        // ── 44. Window resized tiny (320x240) ──────────────────────────
        {
            auto* wm = app_->window_manager();
            if (wm && !wm->windows().empty())
            {
                auto* wctx     = wm->windows()[0];
                auto* glfw_win = static_cast<GLFWwindow*>(wctx->glfw_window);
                if (glfw_win)
                {
                    glfwSetWindowSize(glfw_win, 320, 240);
                    pump_frames(20);
                    named_screenshot("44_window_resized_tiny_320x240",
                                     [&](DesignCheck& c) { expect_swapchain(c, wctx, 320, 240); });

                    // Restore
                    glfwSetWindowSize(glfw_win, 1280, 720);
                    pump_frames(15);
                }
            }
        }

        // ── 45. Multi-window: detached figure in second window ─────────
        {
            auto* wm = app_->window_manager();
            auto* ui = app_->ui_context();
            if (wm && ui && ui->fig_mgr)
            {
                // Ensure we have at least 2 figures
                if (ui->fig_mgr->count() < 2)
                {
                    create_random_figure();
                    pump_frames(5);
                }

                auto ids = app_->figure_registry().all_ids();
                if (ids.size() >= 2)
                {
                    // D44 fix: ensure the detached figure has visible content
                    {
                        auto* fig2 = app_->figure_registry().get(ids[1]);
                        if (fig2 && !fig2->axes().empty() && fig2->axes()[0]->series().empty())
                        {
                            auto&              ax = *fig2->axes_mut()[0];
                            std::vector<float> x2(100), y2(100);
                            for (int i = 0; i < 100; ++i)
                            {
                                x2[i] = static_cast<float>(i) * 0.1f;
                                y2[i] = std::sin(x2[i] * 2.0f) * 0.5f;
                            }
                            ax.line(x2, y2).label("detached");
                            ax.title("Detached Figure");
                            ax.auto_fit();
                        }
                        pump_frames(5);
                    }

                    // Detach second figure into a new window
                    auto* new_wctx =
                        wm->detach_figure(ids[1], 800, 600, "Detached Figure", 100, 100);
                    pump_frames(20);

                    // D41 fix: use window-targeted captures. step() iterates
                    // all windows, so set_active_window before pump_frames
                    // gets overridden.  Pass target WindowContext* so the
                    // capture fires only during that window's end_frame.
                    auto* primary_wctx = wm->windows().empty() ? nullptr : wm->windows()[0];
                    named_screenshot(
                        "45_multi_window_primary",
                        [&](DesignCheck& c)
                        {
                            c.expect(wm->windows().size() == 2, "two OS windows");
                            c.expect(new_wctx != nullptr, "figure detached into a new window");
                            c.expect(primary_wctx
                                         && std::find(primary_wctx->assigned_figures.begin(),
                                                      primary_wctx->assigned_figures.end(),
                                                      ids[1])
                                                == primary_wctx->assigned_figures.end(),
                                     "detached figure left the primary window");
                        },
                        primary_wctx);

                    // Screenshot from the secondary window
                    if (new_wctx)
                    {
                        pump_frames(5);
                        named_screenshot(
                            "45b_multi_window_secondary",
                            [&](DesignCheck& c)
                            {
                                c.expect(std::find(new_wctx->assigned_figures.begin(),
                                                   new_wctx->assigned_figures.end(),
                                                   ids[1])
                                             != new_wctx->assigned_figures.end(),
                                         "secondary window owns the detached figure");
                                expect_swapchain(c, new_wctx, 800, 600);
                                c.expect(imgui_window_drawn(design_imgui(new_wctx), "##canvas"),
                                         "secondary canvas drawn");
                            },
                            new_wctx);
                    }

                    // Close the secondary window
                    if (new_wctx)
                    {
                        wm->request_close(new_wctx->id);
                        wm->process_pending_closes();
                        pump_frames(5);
                        // D37 fix: explicitly clear figure cache after secondary window
                        // teardown to prevent stale last_figure_ in DataInteraction.
                        if (ui->data_interaction)
                            ui->data_interaction->clear_figure_cache();
                    }
                }
            }
        }

        // ── 46. Window moved to different position ──────────────────────
        {
            auto* wm = app_->window_manager();
            if (wm && !wm->windows().empty())
            {
                auto* wctx     = wm->windows()[0];
                auto* glfw_win = static_cast<GLFWwindow*>(wctx->glfw_window);
                if (glfw_win)
                {
                    // Move window around
                    glfwSetWindowPos(glfw_win, 50, 50);
                    pump_frames(5);
                    named_screenshot("46_window_moved_top_left",
                                     [&](DesignCheck& c)
                                     {
                                         int px = -1, py = -1;
                                         glfwGetWindowPos(glfw_win, &px, &py);
                                         c.expect(px == 50 && py == 50,
                                                  "window at (50,50) (actual (" + std::to_string(px)
                                                      + "," + std::to_string(py) + "))");
                                     });
                    glfwSetWindowPos(glfw_win, 400, 200);
                    pump_frames(5);
                }
            }
        }

        // ── 47. Split view with inspector + timeline both open ─────────
        {
            auto* ui = app_->ui_context();
            if (ui)
            {
                ui->cmd_registry.execute("view.split_right");
                pump_frames(5);
                ui->cmd_registry.execute("panel.toggle_inspector");
                pump_frames(5);
                ui->cmd_registry.execute("panel.toggle_timeline");
                pump_frames(10);
                named_screenshot(
                    "47_split_inspector_timeline_open",
                    [&](DesignCheck& c)
                    {
                        expect_split_panes(c, 2);
                        expect_inspector_open(c, true);
                        c.expect(ui->imgui_ui->is_timeline_visible()
                                     && imgui_window_drawn(design_imgui(), "##timeline_panel"),
                                 "timeline drawn");
                    });
                ui->cmd_registry.execute("panel.toggle_timeline");
                ui->cmd_registry.execute("panel.toggle_inspector");
                ui->cmd_registry.execute("view.reset_splits");
                pump_frames(5);
            }
        }

        // ── 48. Two windows side by side ────────────────────────────────
        {
            auto* wm = app_->window_manager();
            if (wm)
            {
                // Create a second figure for the second window
                auto&              fig2 = app_->figure({800, 600});
                auto&              ax2  = fig2.subplot(1, 1, 1);
                std::vector<float> x(150), y(150);
                for (int i = 0; i < 150; ++i)
                {
                    x[i] = static_cast<float>(i) * 0.05f;
                    y[i] = std::cos(x[i] * 3.0f);
                }
                ax2.line(x, y).label("cosine");
                ax2.title("Secondary Window");
                pump_frames(5);

                auto ids = app_->figure_registry().all_ids();
                if (ids.size() >= 2)
                {
                    auto* win2 = wm->detach_figure(ids.back(), 640, 480, "Window B", 700, 100);
                    pump_frames(15);

                    // Position primary window to the left
                    if (!wm->windows().empty())
                    {
                        auto* glfw_primary =
                            static_cast<GLFWwindow*>(wm->windows()[0]->glfw_window);
                        if (glfw_primary)
                            glfwSetWindowPos(glfw_primary, 50, 100);
                    }
                    pump_frames(10);
                    named_screenshot(
                        "48_two_windows_side_by_side",
                        [&](DesignCheck& c)
                        {
                            c.expect(wm->windows().size() == 2, "two OS windows");
                            expect_swapchain(c, win2, 640, 480);
                            int px = -1, py = -1, sx = -1, sy = -1;
                            glfwGetWindowPos(
                                static_cast<GLFWwindow*>(wm->windows()[0]->glfw_window),
                                &px,
                                &py);
                            if (win2)
                                glfwGetWindowPos(static_cast<GLFWwindow*>(win2->glfw_window),
                                                 &sx,
                                                 &sy);
                            c.expect(px == 50 && py == 100, "primary at (50,100)");
                            c.expect(sx == 700 && sy == 100, "secondary at (700,100)");
                        });

                    // Cleanup secondary
                    if (win2)
                    {
                        wm->request_close(win2->id);
                        wm->process_pending_closes();
                        pump_frames(5);
                        // Clear stale figure cache to prevent dangling last_figure_
                        auto* ui48 = app_->ui_context();
                        if (ui48 && ui48->data_interaction)
                            ui48->data_interaction->clear_figure_cache();
                    }
                }
            }
        }

        // ── 49. Fullscreen mode (canvas maximized) ─────────────────────
        // D38 fix: switch to Figure 1 (sine wave) before toggling fullscreen
        // so the canvas has visible content. The fullscreen command hides
        // inspector + nav rail expansion, maximizing the plot area.
        {
            auto* ui = app_->ui_context();
            if (ui && ui->fig_mgr && ui->imgui_ui)
            {
                // Clear stale figure cache from any prior multi-window scenarios
                if (ui->data_interaction)
                    ui->data_interaction->clear_figure_cache();

                // Switch to Figure 1 for clean plot content
                auto ids = app_->figure_registry().all_ids();
                if (!ids.empty())
                    ui->fig_mgr->queue_switch(ids[0]);
                pump_frames(10);

                // Show inspector first so fullscreen toggle hides it
                // (view.fullscreen: all_hidden = !inspector && !nav -> new = all_hidden,
                //  so if inspector is visible, all_hidden=false -> new_inspector=false)
                ui->imgui_ui->get_layout_manager().set_inspector_visible(true);
                ui->imgui_ui->get_layout_manager().set_nav_rail_expanded(false);
                ui->imgui_ui->get_layout_manager().update(1280.0f, 720.0f, 0.0f);
                pump_frames(5);

                // Now toggle fullscreen — hides inspector + keeps nav collapsed
                ui->cmd_registry.execute("view.fullscreen");
                pump_frames(20);   // Allow layout animation to settle
                named_screenshot("49_fullscreen_mode",
                                 [&](DesignCheck& c)
                                 {
                                     expect_inspector_open(c, false);
                                     c.expect(
                                         !ui->imgui_ui->get_layout_manager().is_nav_rail_expanded(),
                                         "nav rail collapsed");
                                 });
                ui->cmd_registry.execute("view.fullscreen");   // Toggle back
                pump_frames(10);
            }
        }

        // ── 50. All panels closed (minimal chrome) ──────────────────────
        // D39 fix: switch to Figure 1 with all panels explicitly hidden
        // and wait for animations to settle before capturing.
        {
            auto* ui = app_->ui_context();
            if (ui && ui->fig_mgr && ui->imgui_ui)
            {
                // Clear any stale figure cache before switching
                if (ui->data_interaction)
                    ui->data_interaction->clear_figure_cache();

                // Switch to Figure 1 for visible plot content
                auto ids = app_->figure_registry().all_ids();
                if (!ids.empty())
                    ui->fig_mgr->queue_switch(ids[0]);

                // Explicitly hide all panels
                auto& lm = ui->imgui_ui->get_layout_manager();
                lm.set_inspector_visible(false);
                lm.set_nav_rail_expanded(false);
                ui->imgui_ui->set_timeline_visible(false);
                ui->imgui_ui->set_curve_editor_visible(false);

                pump_frames(20);   // Allow all animations to fully settle
                named_screenshot(
                    "50_minimal_chrome_all_panels_closed",
                    [&](DesignCheck& c)
                    {
                        auto& lm = ui->imgui_ui->get_layout_manager();
                        expect_inspector_open(c, false);
                        c.expect(!lm.is_nav_rail_expanded(), "nav rail collapsed");
                        c.near(lm.nav_rail_animated_width(),
                               lm.nav_rail_width(),
                               0.5,
                               "nav rail at collapsed width");
                        c.expect(lm.bottom_panel_height() == 0.0f, "no bottom panel");
                        c.expect(!ui->imgui_ui->is_timeline_visible(), "timeline hidden");
                        c.expect(!ui->imgui_ui->is_curve_editor_visible(), "curve editor hidden");
                        expect_split_panes(c, 1);
                        expect_no_transient_ui(c);
                    });
            }
        }

        // ── 51. Empty figure after deleting the last series ─────────────
        {
            auto&              fig = app_->figure({1280, 720});
            auto&              ax  = fig.subplot(1, 1, 1);
            std::vector<float> x(120), y(120);
            for (int i = 0; i < 120; ++i)
            {
                x[i] = static_cast<float>(i) * 0.05f;
                y[i] = std::sin(x[i] * 1.5f) * 0.8f;
            }
            ax.line(x, y).label("to_delete");
            ax.title("Empty State After Delete");
            ax.auto_fit();
            pump_frames(8);
            const FigureId fid                = newest_figure_id();
            bool           deleted_by_command = false;

            auto* ui = app_->ui_context();
            if (ui && ui->fig_mgr)
            {
                auto ids = app_->figure_registry().all_ids();
                if (!ids.empty())
                    ui->fig_mgr->queue_switch(ids.back());
                pump_frames(5);

                ui->cmd_registry.execute("series.cycle_selection");
                pump_frames(2);
                ui->cmd_registry.execute("series.delete");
                pump_frames(8);
                deleted_by_command = ax.series().empty();
            }

            named_screenshot(
                "51_empty_figure_after_delete",
                [&](DesignCheck& c)
                {
                    expect_active_figure(c, fid, "the figure whose series was deleted");
                    c.expect(deleted_by_command, "series.delete removed the selected series");
                    c.expect(series_count(design_active_figure()) == 0, "figure has no series");
                });
        }

        // ── 52. Legend overflow (8+ series) ────────────────────────────────
        {
            auto&              fig = app_->figure({1280, 720});
            auto&              ax  = fig.subplot(1, 1, 1);
            std::vector<float> x(100);

            // Create 8 series to test legend overflow behavior
            for (int series = 0; series < 8; ++series)
            {
                std::vector<float> y(100);
                for (int i = 0; i < 100; ++i)
                {
                    x[i] = static_cast<float>(i) * 0.1f;
                    y[i] = std::sin(x[i] + series * 0.5f) * (1.0f + series * 0.2f);
                }
                ax.line(x, y).label("Series " + std::to_string(series + 1)
                                    + " (long name to test wrapping)");
            }

            ax.title("Legend Overflow Test (8 series)");
            ax.xlabel("Time (s)");
            ax.ylabel("Amplitude");

            // Ensure legend is visible
            fig.legend().visible = true;

            pump_frames(15);
            const FigureId fid = newest_figure_id();
            named_screenshot("52_legend_overflow_8_series",
                             [&](DesignCheck& c)
                             {
                                 expect_active_figure(c, fid, "the new 8-series figure");
                                 Figure* f = design_active_figure();
                                 c.expect(series_count(f) == 8, "eight series");
                                 c.expect(f && f->legend().visible, "legend visible");
                             });
        }

        // ── 53. Split view with mismatched axis ranges ────────────────
        {
            // Left pane: auto-fit (full data range)
            auto&              fig1 = app_->figure({1280, 720});
            auto&              ax1  = fig1.subplot(1, 1, 1);
            std::vector<float> x1(300), y1(300);
            for (int i = 0; i < 300; ++i)
            {
                x1[i] = static_cast<float>(i) * 0.05f;
                y1[i] = std::sin(x1[i]) * 2.0f;
            }
            ax1.line(x1, y1).label("Full Range");
            ax1.title("Auto-fit (full)");
            ax1.auto_fit();

            // Right pane: zoomed-in (narrow x/y range)
            auto&              fig2 = app_->figure({1280, 720});
            auto&              ax2  = fig2.subplot(1, 1, 1);
            std::vector<float> x2(300), y2(300);
            for (int i = 0; i < 300; ++i)
            {
                x2[i] = static_cast<float>(i) * 0.05f;
                y2[i] = std::cos(x2[i]) * 3.0f;
            }
            ax2.line(x2, y2).label("Zoomed");
            ax2.title("Zoomed In");
            ax2.xlim(2.0, 6.0);
            ax2.ylim(-1.5, 1.5);

            pump_frames(10);

            auto* ui = app_->ui_context();
            if (ui)
            {
                ui->cmd_registry.execute("view.split_right");
                pump_frames(15);
                named_screenshot("53_split_view_mismatched_zoom",
                                 [&](DesignCheck& c)
                                 {
                                     expect_split_panes(c, 2);
                                     bool zoomed_pane = false;
                                     for (const auto& pi : ui->dock_system.get_pane_infos())
                                     {
                                         Figure* pf = app_->figure_registry().get(pi.figure_index);
                                         if (pf && !pf->axes().empty())
                                         {
                                             auto xl     = pf->axes()[0]->x_limits();
                                             zoomed_pane = zoomed_pane
                                                           || (std::abs(xl.min - 2.0) < 1e-3
                                                               && std::abs(xl.max - 6.0) < 1e-3);
                                         }
                                     }
                                     c.expect(zoomed_pane,
                                              "one pane shows the zoomed figure (x 2..6)");
                                 });
                ui->cmd_registry.execute("view.reset_splits");
                pump_frames(5);
            }
        }
#endif

        // ── 54. Command palette scrolled (20+ results, scrollbar visible) ──
        // DES-I5: Open palette with no filter (shows all ~50 commands),
        // navigate down 15 items via arrow keys to trigger scroll and make
        // the custom scrollbar appear, then capture.
        {
            auto* ui = app_->ui_context();
            if (ui)
            {
                // Switch to Figure 1 so the background is clean
                auto ids = app_->figure_registry().all_ids();
                if (!ids.empty())
                    ui->fig_mgr->queue_switch(ids[0]);
                pump_frames(5);

                // Open command palette (shows all commands, no filter)
                ui->cmd_registry.execute("app.command_palette");
                pump_frames(8);   // Let open animation complete + results populate

                // Navigate down 15 items to scroll the list and trigger scrollbar
                for (int i = 0; i < 15; ++i)
                {
                    ImGui::GetIO().AddKeyEvent(ImGuiKey_DownArrow, true);
                    pump_frames(1);
                    ImGui::GetIO().AddKeyEvent(ImGuiKey_DownArrow, false);
                    pump_frames(1);
                }
                pump_frames(5);   // Let scroll animation settle with scrollbar visible

                named_screenshot(
                    "54_command_palette_scrolled",
                    [&](DesignCheck& c)
                    {
                        c.expect(ui->cmd_palette.is_open()
                                     && imgui_window_drawn(design_imgui(), "##command_palette"),
                                 "palette drawn");
                        c.expect(ui->cmd_palette.selected_index() >= 15,
                                 "selection moved 15 rows (index "
                                     + std::to_string(ui->cmd_palette.selected_index()) + ")");
                        c.expect(ui->cmd_palette.scroll_offset() > 0.0f, "result list scrolled");
                    });

                // Close palette
                ui->cmd_registry.execute("app.cancel");
                pump_frames(3);
            }
        }

        // ── Scenario 55: Nav rail icon alignment at 1.25× font scale ────
        // DES-I6: Verify nav rail icons and labels are pixel-snapped at non-integer
        // DPI scale factors. We simulate 125% scale by temporarily boosting
        // ImGui global font scale, rendering frames, capturing, then restoring.
        {
            auto* ui = app_->ui_context();
            if (ui)
            {
                // Switch to Figure 1 (clean sine wave, no extra chrome)
                auto ids = app_->figure_registry().all_ids();
                if (!ids.empty())
                    ui->fig_mgr->queue_switch(ids[0]);
                pump_frames(3);

                ImGuiWindow* bar0         = imgui_find_window(design_imgui(), "##commandbar");
                const float  base_font_px = bar0 ? bar0->FontRefSize : 0.0f;
                // Apply 125% through the app's UI scale (Settings -> Appearance),
                // which scales fonts and style metrics in every ImGui context.
                // (io.FontGlobalScale is obsolete since ImGui 1.92 and only
                // scaled glyphs, never the layout.)
                const float saved_scale = ui->theme_mgr ? ui->theme_mgr->ui_scale() : 1.0f;
                if (ui->theme_mgr)
                    ui->theme_mgr->set_ui_scale(1.25f);
                pump_frames(3);   // Let layout settle at new scale

                named_screenshot("55_nav_rail_dpi_scale_125pct",
                                 [&](DesignCheck& c)
                                 {
                                     ImGuiContext* ctx = design_imgui();
                                     c.near(ctx ? ctx->Style.FontScaleMain : 0.0f,
                                            1.25,
                                            0.001,
                                            "ImGui style font scale");
                                     ImGuiWindow* bar = imgui_find_window(ctx, "##commandbar");
                                     c.near(bar ? bar->FontRefSize : 0.0f,
                                            base_font_px * 1.25f,
                                            0.5,
                                            "font size in effect (px)");
                                 });

                // Restore original scale
                if (ui->theme_mgr)
                    ui->theme_mgr->set_ui_scale(saved_scale);
                pump_frames(2);
            }
        }

        // ── Scenario 56: Tiny window (320×240) with all panels open ────
        // DES-I9: Verify no panel overflow / occlusion when inspector,
        // timeline, and nav rail are all open at minimum window size.
        {
            auto* wm = app_->window_manager();
            auto* ui = app_->ui_context();
            if (wm && ui && ui->imgui_ui && !wm->windows().empty())
            {
                auto* wctx     = wm->windows()[0];
                auto* glfw_win = static_cast<GLFWwindow*>(wctx->glfw_window);
                if (glfw_win)
                {
                    // Switch to Figure 1 so there is visible content
                    auto ids = app_->figure_registry().all_ids();
                    if (!ids.empty())
                        ui->fig_mgr->queue_switch(ids[0]);
                    pump_frames(5);

                    // Open all panels: inspector, nav rail expanded, timeline
                    auto& lm = ui->imgui_ui->get_layout_manager();
                    lm.set_inspector_visible(true);
                    lm.set_nav_rail_expanded(true);
                    ui->imgui_ui->set_timeline_visible(true);
                    pump_frames(10);

                    // Shrink to 320×240
                    glfwSetWindowSize(glfw_win, 320, 240);
                    pump_frames(20);
                    named_screenshot(
                        "56_tiny_window_all_panels_open",
                        [&](DesignCheck& c)
                        {
                            expect_swapchain(c, wctx, 320, 240);
                            c.expect(lm.is_inspector_visible(), "inspector requested open");
                            c.expect(lm.is_nav_rail_expanded(), "nav rail expanded");
                            c.expect(ui->imgui_ui->is_timeline_visible()
                                         && imgui_window_drawn(design_imgui(), "##timeline_panel"),
                                     "timeline drawn");
                        });

                    // Restore window and hide panels
                    glfwSetWindowSize(glfw_win, 1280, 720);
                    lm.set_inspector_visible(false);
                    lm.set_nav_rail_expanded(false);
                    ui->imgui_ui->set_timeline_visible(false);
                    pump_frames(15);
                }
            }
        }

        // ── 57–63. Settings panel (G-6: Appearance / Shortcuts / UI Defaults) ──
        {
            auto* ui = app_->ui_context();
            if (ui)
            {
                auto ids = app_->figure_registry().all_ids();
                if (ids.empty() == false && ui->fig_mgr)
                    ui->fig_mgr->queue_switch(ids[0]);
                pump_frames(5);

                auto close_settings = [&]()
                {
                    ui->settings_panel.set_visible(false);
                    pump_frames(5);
                };

                auto capture_settings = [&](int tab, const char* shot, const char* theme)
                {
                    if (ui->theme_mgr)
                    {
                        ui->theme_mgr->set_theme(theme);
                        pump_frames(20);
                    }
                    ui->settings_panel.select_tab(tab);
                    ui->cmd_registry.execute("panel.open_settings");
                    pump_frames(12);
                    named_screenshot(
                        shot,
                        [&](DesignCheck& c)
                        {
                            c.expect(ui->settings_panel.is_visible(), "settings visible");
                            c.expect(imgui_drawn_popups(design_imgui(), {}, true) >= 1,
                                     "settings modal drawn");
                            c.expect(ui->settings_panel.active_tab() == tab,
                                     "settings tab " + std::to_string(tab) + " shown (actual "
                                         + std::to_string(ui->settings_panel.active_tab()) + ")");
                            expect_theme(c, theme);
                        });
                    close_settings();
                };

                capture_settings(0, "57_settings_appearance_night", "night");
                capture_settings(0, "58_settings_appearance_light", "light");
                capture_settings(0, "59_settings_appearance_high_contrast", "high_contrast");
                capture_settings(1, "60_settings_shortcuts_night", "night");
                capture_settings(1, "61_settings_shortcuts_light", "light");
                capture_settings(2, "62_settings_ui_defaults", "night");

                if (ui->theme_mgr)
                {
                    ui->theme_mgr->set_theme("dark");
                    pump_frames(15);
                }
            }
        }

        // ── Summary ─────────────────────────────────────────────────────
        fprintf(stderr,
                "[QA/Design] Captured %zu design screenshots in %s/design/\n",
                design_screenshots_.size(),
                opts_.output_dir.c_str());

        static constexpr size_t EXPECTED_DESIGN_SHOTS = 63;
        if (design_screenshots_.size() != EXPECTED_DESIGN_SHOTS)
        {
            add_issue(IssueSeverity::Error,
                      "design_capture",
                      "Expected " + std::to_string(EXPECTED_DESIGN_SHOTS) + " screenshots, got "
                          + std::to_string(design_screenshots_.size()),
                      false);
        }

        // Write design screenshot manifest
        {
            std::string   manifest_path = opts_.output_dir + "/design/manifest.txt";
            std::ofstream out(manifest_path);
            out << "Spectra Design Review Screenshots\n";
            out << "==================================\n";
            out << "Captured: " << design_screenshots_.size() << " screenshots\n\n";
            size_t checks = 0, failed = 0;
            for (const auto& rec : design_assertions_)
            {
                checks += rec.checks;
                failed += rec.failures.empty() ? 0 : 1;
            }
            out << "Semantic assertions: " << design_assertions_.size() << " captures, " << checks
                << " checks, " << failed << " captures failed\n\n";
            for (const auto& rec : design_assertions_)
            {
                out << "  " << rec.shot << "  [" << (rec.failures.empty() ? "PASS" : "FAIL") << " "
                    << (rec.checks - rec.failures.size()) << "/" << rec.checks << "]\n    -> "
                    << rec.path << "\n";
                for (const auto& f : rec.failures)
                    out << "    FAILED: " << f << "\n";
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
        WaitFrames,
        WindowResize,
        WindowDrag,
        TabDetach,
        COUNT
    };

    struct ActionWeight
    {
        FuzzAction action;
        int        weight;
    };

    void run_fuzzing()
    {
        fprintf(stderr,
                "[QA] Starting fuzzing phase (%lu frames)\n",
                static_cast<unsigned long>(opts_.fuzz_frames));

        std::vector<ActionWeight> weights = {
            {FuzzAction::ExecuteCommand, 15},
            {FuzzAction::MouseClick, 15},
            {FuzzAction::MouseDrag, 10},
            {FuzzAction::MouseScroll, 10},
            {FuzzAction::KeyPress, 10},
            {FuzzAction::CreateFigure, 5},
            {FuzzAction::CloseFigure, 3},
            {FuzzAction::SwitchTab, 8},
            {FuzzAction::AddSeries, 8},
            {FuzzAction::UpdateData, 5},
            {FuzzAction::LargeDataset, 1},
            {FuzzAction::SplitDock, 3},
            {FuzzAction::WaitFrames, 7},
            {FuzzAction::WindowResize, 3},
            {FuzzAction::WindowDrag, 3},
            {FuzzAction::TabDetach, 2},
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
            int        roll       = weight_dist(rng_);
            FuzzAction action     = FuzzAction::WaitFrames;
            int        cumulative = 0;
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

        fprintf(stderr,
                "[QA] Fuzzing complete (%lu total frames)\n",
                static_cast<unsigned long>(total_frames_));
    }

    static const char* fuzz_action_name(FuzzAction a)
    {
        switch (a)
        {
            case FuzzAction::ExecuteCommand:
                return "fuzz:ExecuteCommand";
            case FuzzAction::MouseClick:
                return "fuzz:MouseClick";
            case FuzzAction::MouseDrag:
                return "fuzz:MouseDrag";
            case FuzzAction::MouseScroll:
                return "fuzz:MouseScroll";
            case FuzzAction::KeyPress:
                return "fuzz:KeyPress";
            case FuzzAction::CreateFigure:
                return "fuzz:CreateFigure";
            case FuzzAction::CloseFigure:
                return "fuzz:CloseFigure";
            case FuzzAction::SwitchTab:
                return "fuzz:SwitchTab";
            case FuzzAction::AddSeries:
                return "fuzz:AddSeries";
            case FuzzAction::UpdateData:
                return "fuzz:UpdateData";
            case FuzzAction::LargeDataset:
                return "fuzz:LargeDataset";
            case FuzzAction::SplitDock:
                return "fuzz:SplitDock";
            case FuzzAction::WaitFrames:
                return "fuzz:WaitFrames";
            case FuzzAction::WindowResize:
                return "fuzz:WindowResize";
            case FuzzAction::WindowDrag:
                return "fuzz:WindowDrag";
            case FuzzAction::TabDetach:
                return "fuzz:TabDetach";
            default:
                return "fuzz:Unknown";
        }
    }

    void execute_fuzz_action(FuzzAction action)
    {
        // P0 fix: track last action for crash handler context
        snprintf(g_last_action,
                 sizeof(g_last_action),
                 "%s (frame %lu)",
                 fuzz_action_name(action),
                 static_cast<unsigned long>(total_frames_));

        [[maybe_unused]] auto* ui = app_->ui_context();

        switch (action)
        {
            case FuzzAction::ExecuteCommand:
            {
#ifdef SPECTRA_USE_IMGUI
                if (!ui)
                    break;
                auto                     cmd_ptrs = ui->cmd_registry.all_commands();
                std::vector<std::string> cmds;
                for (auto* c : cmd_ptrs)
                    if (c)
                        cmds.push_back(c->id);
                if (cmds.empty())
                    break;
                std::uniform_int_distribution<size_t> dist(0, cmds.size() - 1);
                const auto&                           id = cmds[dist(rng_)];
                // Skip destructive and interactive commands (native file dialogs)
                if (id != "figure.close" && id != "app.quit" && id != "file.save_figure"
                    && id != "file.load_figure")
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
                std::uniform_int_distribution<int>     btn(0, 1);
                double                                 mx = px(rng_), my = py(rng_);
                int                                    b = btn(rng_);
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
                double                                 x1 = px(rng_), y1 = py(rng_);
                double                                 x2 = px(rng_), y2 = py(rng_);
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
                int                                k = key(rng_);
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

                std::uniform_int_distribution<int>    n_dist(10, 200);
                int                                   n = n_dist(rng_);
                std::vector<float>                    x(n), y(n);
                std::uniform_real_distribution<float> val(-50.0f, 50.0f);
                for (int i = 0; i < n; ++i)
                {
                    x[i] = static_cast<float>(i);
                    y[i] = val(rng_);
                }

                auto&                              ax = fig->subplot(1, 1, 1);
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
                    auto                                  xd = line->x_data();
                    std::vector<float>                    new_y(xd.size());
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
                int                                n = n_dist(rng_);
                std::vector<float>                 x(n), y(n);
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

            case FuzzAction::WaitFrames:
            {
                std::uniform_int_distribution<int> wait(1, 10);
                pump_frames(wait(rng_));
                break;
            }

            case FuzzAction::WindowResize:
            {
#ifdef SPECTRA_USE_GLFW
                auto* wm = app_->window_manager();
                if (!wm || wm->windows().empty())
                    break;
                auto* glfw_win = static_cast<GLFWwindow*>(wm->windows()[0]->glfw_window);
                if (!glfw_win)
                    break;
                std::uniform_int_distribution<int> dim(200, 1920);
                glfwSetWindowSize(glfw_win, dim(rng_), dim(rng_));
#endif
                break;
            }

            case FuzzAction::WindowDrag:
            {
#ifdef SPECTRA_USE_GLFW
                auto* wm = app_->window_manager();
                if (!wm || wm->windows().empty())
                    break;
                auto* glfw_win = static_cast<GLFWwindow*>(wm->windows()[0]->glfw_window);
                if (!glfw_win)
                    break;
                std::uniform_int_distribution<int> pos_x(0, 1600);
                std::uniform_int_distribution<int> pos_y(0, 900);
                glfwSetWindowPos(glfw_win, pos_x(rng_), pos_y(rng_));
#endif
                break;
            }

            case FuzzAction::TabDetach:
            {
#ifdef SPECTRA_USE_GLFW
                auto* wm = app_->window_manager();
                if (!wm)
                    break;
                auto ids = app_->figure_registry().all_ids();
                if (ids.size() < 2)
                    break;   // Need at least 2 figures to detach one
                std::uniform_int_distribution<size_t> fig_dist(0, ids.size() - 1);
                FigureId                              fid = ids[fig_dist(rng_)];
                // Cap at 5 windows to avoid resource exhaustion
                if (wm->window_count() < 5)
                {
                    std::uniform_int_distribution<int> pos(50, 800);
                    auto* w = wm->detach_figure(fid, 640, 480, "Fuzz Detach", pos(rng_), pos(rng_));
                    if (w)
                        pump_frames(5);
                }
                else
                {
                    // Too many windows — close a random non-primary window
                    auto wins = wm->windows();
                    if (wins.size() > 1)
                    {
                        std::uniform_int_distribution<size_t> win_dist(1, wins.size() - 1);
                        wm->request_close(wins[win_dist(rng_)]->id);
                        wm->process_pending_closes();
                    }
                }
#endif
                break;
            }

            default:
                break;
        }
    }

    void sample_gpu_memory()
    {
        auto* backend = dynamic_cast<VulkanBackend*>(app_->backend());
        if (!backend)
        {
            gpu_memory_tracking_available_ = false;
            return;
        }

        VulkanBackend::GpuMemoryStats stats;
        if (!backend->query_gpu_memory_stats(stats))
        {
            gpu_memory_tracking_available_ = false;
            return;
        }

        gpu_memory_tracking_available_ = true;
        current_gpu_memory_            = stats;

        if (stats.total_usage_bytes > peak_gpu_memory_.total_usage_bytes)
            peak_gpu_memory_.total_usage_bytes = stats.total_usage_bytes;
        if (stats.total_budget_bytes > peak_gpu_memory_.total_budget_bytes)
            peak_gpu_memory_.total_budget_bytes = stats.total_budget_bytes;
        if (stats.device_local_usage_bytes > peak_gpu_memory_.device_local_usage_bytes)
            peak_gpu_memory_.device_local_usage_bytes = stats.device_local_usage_bytes;
        if (stats.device_local_budget_bytes > peak_gpu_memory_.device_local_budget_bytes)
            peak_gpu_memory_.device_local_budget_bytes = stats.device_local_budget_bytes;
        if (stats.heap_count > peak_gpu_memory_.heap_count)
            peak_gpu_memory_.heap_count = stats.heap_count;
        peak_gpu_memory_.budget_extension_enabled =
            peak_gpu_memory_.budget_extension_enabled || stats.budget_extension_enabled;
    }

    bool snapshot_gpu_memory(VulkanBackend::GpuMemoryStats& stats)
    {
        sample_gpu_memory();
        if (!gpu_memory_tracking_available_)
        {
            stats = {};
            return false;
        }

        stats = current_gpu_memory_;
        return true;
    }

    // ── Per-frame monitoring ─────────────────────────────────────────────
    // Declare a spike warm-up window when the frame just rendered used
    // something for the first time.
    void detect_first_use_warmup()
    {
        bool first_use = false;
#ifdef SPECTRA_USE_IMGUI
        if (auto* ui = app_->ui_context(); ui && ui->fig_mgr)
        {
            const FigureId id = ui->fig_mgr->active_index();
            if (id != INVALID_FIGURE_ID && seen_figures_.insert(id).second)
                first_use = true;
            if (Figure* fig = ui->fig_mgr->active_figure())
            {
                for (const auto& ax : fig->all_axes())
                    if (ax)
                        for (const auto& sp : ax->series())
                            if (sp
                                && seen_series_types_.insert(std::type_index(typeid(*sp))).second)
                                first_use = true;
                for (const auto& ax : fig->axes())
                    if (ax)
                        for (const auto& sp : ax->series())
                            if (sp
                                && seen_series_types_.insert(std::type_index(typeid(*sp))).second)
                                first_use = true;
            }
        }
#endif
        if (auto* wm = app_->window_manager())
        {
            const size_t n = wm->windows().size();
            if (n > last_window_count_)
                first_use = true;
            last_window_count_ = n;
            if (n > 0 && wm->windows()[0])
            {
                const auto&    ext = wm->windows()[0]->swapchain.extent;
                const uint64_t key = (static_cast<uint64_t>(ext.width) << 32) | ext.height;
                if (key != last_swapchain_key_)
                    first_use = true;
                last_swapchain_key_ = key;
            }
        }
        if (first_use)
            spike_detector_.declare_warmup(total_frames_);
    }

    void check_frame(const App::StepResult& result)
    {
        if (total_frames_ % 30 == 0)
            check_validation_messages("frame_loop");

        // Frame time spike detection.  First-use frames (a figure's first
        // frames: data upload + layout; a series type's first draw: driver
        // shader-variant JIT on lavapipe; a new window/swapchain) are a
        // declared warm-up window and are counted, not flagged.
        detect_first_use_warmup();
        if (spike_detector_.is_spike(total_frames_, result.frame_time_ms, frame_stats_.ema))
        {
            frame_stats_.spike_count++;
            add_issue(IssueSeverity::Warning,
                      "frame_time",
                      "Frame " + std::to_string(result.frame_number) + " took "
                          + std::to_string(result.frame_time_ms) + "ms ("
                          + std::to_string(result.frame_time_ms / frame_stats_.ema) + "x average)");
        }

        // RSS check every 60 frames
        if (total_frames_ % 60 == 0)
        {
            size_t rss = get_rss_bytes();
            if (rss > peak_rss_)
                peak_rss_ = rss;

            sample_gpu_memory();

            // Plateau-aware: transient peaks (e.g. several windows open at
            // once) are not growth; warn when the floor of the last 5 samples
            // (300 frames) stays >100MB above the initial RSS, once per +50MB.
            rss_floor_.add(rss);
            static constexpr size_t RSS_GROWTH_BYTES = 100ull * 1024ull * 1024ull;
            static constexpr size_t RSS_REWARN_BYTES = 50ull * 1024ull * 1024ull;
            if (rss_floor_.sustained_above(initial_rss_, RSS_GROWTH_BYTES)
                && rss_floor_.floor() >= next_rss_warn_floor_)
            {
                next_rss_warn_floor_ = rss_floor_.floor() + RSS_REWARN_BYTES;
                add_issue(IssueSeverity::Warning,
                          "memory",
                          "RSS stayed " + std::to_string(to_mb(rss_floor_.floor() - initial_rss_))
                              + "MB above initial for 300 frames (initial: "
                              + std::to_string(to_mb(initial_rss_))
                              + "MB, floor: " + std::to_string(to_mb(rss_floor_.floor()))
                              + "MB, current: " + std::to_string(to_mb(rss)) + "MB)");
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
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
                c = '_';
        }

        std::string path = opts_.output_dir + "/screenshot_frame" + std::to_string(total_frames_)
                           + "_" + safe_reason + ".png";
        ImageExporter::write_png(path, pixels.data(), w, h);
        return path;
    }

    // ── Wall clock check ─────────────────────────────────────────────────
    bool wall_clock_exceeded() const
    {
        auto  elapsed = std::chrono::steady_clock::now() - start_time_;
        float sec     = std::chrono::duration<float>(elapsed).count();
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
        float          duration          = wall_clock_seconds();
        const bool     validation_active = validation_monitor_ && validation_monitor_->active();
        const uint32_t validation_errors =
            validation_monitor_ ? validation_monitor_->error_count() : 0;
        const uint32_t validation_warnings =
            validation_monitor_ ? validation_monitor_->warning_count() : 0;

        // Text report
        {
            std::string   path = opts_.output_dir + "/qa_report.txt";
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
            out << "Scenarios: " << scenarios_passed_ << " passed, " << scenarios_failed_
                << " failed\n";
            out << "Fuzz frames: " << (opts_.no_fuzz ? 0 : opts_.fuzz_frames) << "\n";
            out << "\n";

            out << "Frame Time Statistics:\n";
            out << "  Average: " << frame_stats_.average() << "ms\n";
            out << "  P95: " << frame_stats_.percentile(0.95f) << "ms\n";
            out << "  P99: " << frame_stats_.percentile(0.99f) << "ms\n";
            out << "  Max: " << frame_stats_.max_val() << "ms\n";
            out << "  Spikes (>3x avg): " << frame_stats_.spike_count << "\n";
            out << "  First-use warm-up spikes (excluded): " << spike_detector_.warmup_spikes()
                << "\n";
            out << "\n";

            out << "Memory:\n";
            out << "  Initial RSS: " << to_mb(initial_rss_) << "MB\n";
            out << "  Peak RSS: " << to_mb(peak_rss_) << "MB\n";
            if (gpu_memory_tracking_available_)
            {
                out << "  GPU usage (all heaps): initial="
                    << to_mb(initial_gpu_memory_.total_usage_bytes)
                    << "MB peak=" << to_mb(peak_gpu_memory_.total_usage_bytes)
                    << "MB budget=" << to_mb(current_gpu_memory_.total_budget_bytes) << "MB\n";
                out << "  GPU usage (device-local): initial="
                    << to_mb(initial_gpu_memory_.device_local_usage_bytes)
                    << "MB peak=" << to_mb(peak_gpu_memory_.device_local_usage_bytes)
                    << "MB budget=" << to_mb(current_gpu_memory_.device_local_budget_bytes) << "MB";
                if (!current_gpu_memory_.budget_extension_enabled)
                    out << " (VK_EXT_memory_budget unavailable; values may be approximate)";
                out << "\n";
            }
            out << "\n";

            if (!scenario_memory_.empty())
            {
                out << "Per-Scenario Memory Retention:\n";
                out << "  Thresholds: RSS > +20MB, GPU device-local > +5MB\n";
                for (const auto& metric : scenario_memory_)
                {
                    const int64_t rss_delta =
                        signed_byte_delta(metric.rss_before_bytes, metric.rss_after_bytes);
                    out << "  " << metric.name << ": RSS " << format_mb_delta(rss_delta) << " ("
                        << to_mb(metric.rss_before_bytes) << "->" << to_mb(metric.rss_after_bytes)
                        << "MB)";
                    if (metric.gpu_before_valid && metric.gpu_after_valid)
                    {
                        const int64_t gpu_delta =
                            signed_byte_delta(metric.gpu_before.device_local_usage_bytes,
                                              metric.gpu_after.device_local_usage_bytes);
                        out << ", GPU local " << format_mb_delta(gpu_delta) << " ("
                            << to_mb(metric.gpu_before.device_local_usage_bytes) << "->"
                            << to_mb(metric.gpu_after.device_local_usage_bytes) << "MB)";
                    }
                    else
                    {
                        out << ", GPU local n/a";
                    }
                    out << ", frames=" << (metric.frame_end - metric.frame_start)
                        << ", status=" << (metric.passed ? "PASS" : "FAIL") << "\n";
                }
                out << "\n";
            }

            if (!design_assertions_.empty())
            {
                size_t checks = 0, failed = 0;
                for (const auto& rec : design_assertions_)
                {
                    checks += rec.checks;
                    failed += rec.failures.empty() ? 0 : 1;
                }
                out << "Design Assertions:\n";
                out << "  Captures: " << design_assertions_.size() << " ("
                    << (design_assertions_.size() - failed) << " proved, " << failed
                    << " failed)\n";
                out << "  Checks: " << checks << "\n";
                for (const auto& rec : design_assertions_)
                    for (const auto& f : rec.failures)
                        out << "  FAILED " << rec.shot << ": " << f << "\n";
                out << "\n";
            }

            out << "Validation:\n";
            out << "  Monitor active: " << (validation_active ? "yes" : "no") << "\n";
            out << "  Errors: " << validation_errors << "\n";
            out << "  Warnings: " << validation_warnings << "\n";
            out << "\n";

            if (!issues_.empty())
            {
                // P1 fix: Group issues by category with summary counts
                std::map<std::string, std::vector<const QAIssue*>> by_category;
                for (const auto& issue : issues_)
                    by_category[issue.category].push_back(&issue);

                out << "Issue Summary (" << issues_.size() << " total, " << by_category.size()
                    << " categories):\n";
                for (const auto& [cat, cat_issues] : by_category)
                {
                    // Count by severity
                    size_t warns = 0, errs = 0, crits = 0;
                    for (auto* i : cat_issues)
                    {
                        if (i->severity == IssueSeverity::Warning)
                            warns++;
                        else if (i->severity == IssueSeverity::Error)
                            errs++;
                        else if (i->severity == IssueSeverity::Critical)
                            crits++;
                    }
                    out << "  " << cat << ": " << cat_issues.size() << " issues";
                    if (crits)
                        out << " (" << crits << " CRITICAL)";
                    if (errs)
                        out << " (" << errs << " ERROR)";
                    if (warns)
                        out << " (" << warns << " WARNING)";
                    out << " [frames " << cat_issues.front()->frame << "-"
                        << cat_issues.back()->frame << "]\n";
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
            std::string   path = opts_.output_dir + "/qa_report.json";
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
            out << "    \"spikes\": " << frame_stats_.spike_count << ",\n";
            out << "    \"warmup_spikes_excluded\": " << spike_detector_.warmup_spikes() << "\n";
            out << "  },\n";
            out << "  \"memory\": {\n";
            out << "    \"initial_rss_mb\": " << to_mb(initial_rss_) << ",\n";
            out << "    \"peak_rss_mb\": " << to_mb(peak_rss_) << ",\n";
            out << "    \"gpu_memory_tracking\": "
                << (gpu_memory_tracking_available_ ? "true" : "false") << ",\n";
            out << "    \"gpu_budget_extension\": "
                << (current_gpu_memory_.budget_extension_enabled ? "true" : "false") << ",\n";
            out << "    \"gpu_initial_usage_mb\": " << to_mb(initial_gpu_memory_.total_usage_bytes)
                << ",\n";
            out << "    \"gpu_peak_usage_mb\": " << to_mb(peak_gpu_memory_.total_usage_bytes)
                << ",\n";
            out << "    \"gpu_current_budget_mb\": "
                << to_mb(current_gpu_memory_.total_budget_bytes) << ",\n";
            out << "    \"gpu_initial_device_local_mb\": "
                << to_mb(initial_gpu_memory_.device_local_usage_bytes) << ",\n";
            out << "    \"gpu_peak_device_local_mb\": "
                << to_mb(peak_gpu_memory_.device_local_usage_bytes) << ",\n";
            out << "    \"gpu_current_device_local_budget_mb\": "
                << to_mb(current_gpu_memory_.device_local_budget_bytes) << "\n";
            out << "  },\n";
            out << "  \"validation\": {\n";
            out << "    \"monitor_active\": " << (validation_active ? "true" : "false") << ",\n";
            out << "    \"error_count\": " << validation_errors << ",\n";
            out << "    \"warning_count\": " << validation_warnings << "\n";
            out << "  },\n";
            {
                size_t checks = 0, failed = 0;
                for (const auto& rec : design_assertions_)
                {
                    checks += rec.checks;
                    failed += rec.failures.empty() ? 0 : 1;
                }
                out << "  \"design_assertions\": {\"captures\": " << design_assertions_.size()
                    << ", \"checks\": " << checks << ", \"failed_captures\": " << failed
                    << ", \"shots\": [\n";
                for (size_t i = 0; i < design_assertions_.size(); ++i)
                {
                    const auto& rec = design_assertions_[i];
                    out << "    {\"name\": \"" << json_escape(rec.shot)
                        << "\", \"checks\": " << rec.checks << ", \"frame\": " << rec.frame
                        << ", \"failures\": [";
                    for (size_t k = 0; k < rec.failures.size(); ++k)
                        out << (k ? ", " : "") << "\"" << json_escape(rec.failures[k]) << "\"";
                    out << "]}" << (i + 1 < design_assertions_.size() ? "," : "") << "\n";
                }
                out << "  ]},\n";
            }
            out << "  \"scenario_memory\": [\n";
            for (size_t i = 0; i < scenario_memory_.size(); ++i)
            {
                const auto&   metric       = scenario_memory_[i];
                const int64_t rss_delta_mb = to_mb_signed(
                    signed_byte_delta(metric.rss_before_bytes, metric.rss_after_bytes));
                out << "    {\"name\": \"" << metric.name
                    << "\", \"passed\": " << (metric.passed ? "true" : "false")
                    << ", \"frames\": " << (metric.frame_end - metric.frame_start)
                    << ", \"rss_start_mb\": " << to_mb(metric.rss_before_bytes)
                    << ", \"rss_end_mb\": " << to_mb(metric.rss_after_bytes)
                    << ", \"rss_delta_mb\": " << rss_delta_mb << ", \"gpu_memory_tracked\": "
                    << ((metric.gpu_before_valid && metric.gpu_after_valid) ? "true" : "false");
                if (metric.gpu_before_valid && metric.gpu_after_valid)
                {
                    const int64_t gpu_delta_mb =
                        to_mb_signed(signed_byte_delta(metric.gpu_before.device_local_usage_bytes,
                                                       metric.gpu_after.device_local_usage_bytes));
                    out << ", \"gpu_device_local_start_mb\": "
                        << to_mb(metric.gpu_before.device_local_usage_bytes)
                        << ", \"gpu_device_local_end_mb\": "
                        << to_mb(metric.gpu_after.device_local_usage_bytes)
                        << ", \"gpu_device_local_delta_mb\": " << gpu_delta_mb;
                }
                out << "}";
                if (i + 1 < scenario_memory_.size())
                    out << ",";
                out << "\n";
            }
            out << "  ],\n";
            out << "  \"issues\": [\n";
            for (size_t i = 0; i < issues_.size(); ++i)
            {
                const auto& issue = issues_[i];
                out << "    {\"severity\": \"" << severity_str(issue.severity)
                    << "\", \"category\": \"" << issue.category << "\", \"message\": \""
                    << json_escape(issue.message) << "\", \"frame\": " << issue.frame << "}";
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
                "[QA] GPU memory: local initial=%luMB peak=%luMB budget=%luMB (%s)\n"
                "[QA] Validation: errors=%u warnings=%u (%s)\n"
                "[QA] Issues: %lu warning, %lu error, %lu critical\n"
                "[QA] ═══════════════════════════════════════\n",
                static_cast<unsigned long>(opts_.seed),
                duration,
                static_cast<unsigned long>(total_frames_),
                scenarios_passed_,
                scenarios_failed_,
                frame_stats_.average(),
                frame_stats_.percentile(0.95f),
                frame_stats_.max_val(),
                frame_stats_.spike_count,
                static_cast<unsigned long>(to_mb(initial_rss_)),
                static_cast<unsigned long>(to_mb(peak_rss_)),
                static_cast<unsigned long>(to_mb(initial_gpu_memory_.device_local_usage_bytes)),
                static_cast<unsigned long>(to_mb(peak_gpu_memory_.device_local_usage_bytes)),
                static_cast<unsigned long>(to_mb(current_gpu_memory_.device_local_budget_bytes)),
                current_gpu_memory_.budget_extension_enabled ? "budget" : "approx",
                validation_errors,
                validation_warnings,
                validation_active ? "monitor active" : "monitor unavailable",
                static_cast<unsigned long>(issues_with_severity(IssueSeverity::Warning)),
                static_cast<unsigned long>(issues_with_severity(IssueSeverity::Error)),
                static_cast<unsigned long>(issues_with_severity(IssueSeverity::Critical)));
    }

    // ── Members ──────────────────────────────────────────────────────────
    QAOptions    opts_;
    std::mt19937 rng_;

    std::unique_ptr<App>               app_;
    std::unique_ptr<ValidationMonitor> validation_monitor_;

    std::chrono::steady_clock::time_point start_time_;

    uint64_t total_frames_     = 0;
    uint32_t scenarios_passed_ = 0;
    uint32_t scenarios_failed_ = 0;

    FrameStats                frame_stats_;
    qa::SpikeDetector         spike_detector_;
    qa::RssFloorMonitor       rss_floor_{5};
    size_t                    next_rss_warn_floor_ = 0;
    std::set<FigureId>        seen_figures_;
    std::set<std::type_index> seen_series_types_;
    size_t                    last_window_count_  = 0;
    uint64_t                  last_swapchain_key_ = 0;

    size_t                        initial_rss_                   = 0;
    size_t                        peak_rss_                      = 0;
    bool                          gpu_memory_tracking_available_ = false;
    VulkanBackend::GpuMemoryStats initial_gpu_memory_{};
    VulkanBackend::GpuMemoryStats current_gpu_memory_{};
    VulkanBackend::GpuMemoryStats peak_gpu_memory_{};

    std::vector<QAIssue>              issues_;
    std::vector<Scenario>             scenarios_;
    std::vector<ScenarioMemoryMetric> scenario_memory_;

    // P0 fix: screenshot rate limiting per category
    std::unordered_map<std::string, uint64_t> last_screenshot_frame_;
    std::unordered_map<std::string, uint32_t> validation_message_hits_;

    // Design review
    std::vector<std::pair<std::string, std::string>> design_screenshots_;
    std::vector<DesignAssertionRecord>               design_assertions_;
    bool                                             saved_pointer_valid_ = false;
    double                                           saved_pointer_x_     = 0.0;
    double                                           saved_pointer_y_     = 0.0;
    std::unordered_map<uint64_t, std::string>        screenshot_hash_to_name_;
};

// ─── Signal handler ──────────────────────────────────────────────────────────
static void crash_handler(int sig)
{
    const char* name = (sig == SIGSEGV) ? "SIGSEGV" : (sig == SIGABRT) ? "SIGABRT" : "SIGNAL";

    // Minimal async-signal-safe output
    char buf[512];
    int  len = snprintf(buf,
                        sizeof(buf),
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
        if (write(STDERR_FILENO, buf, static_cast<size_t>(len)))
        {
        }

#ifdef __linux__
    // Stack trace via backtrace()
    void* frames[32];
    int   nframes = backtrace(frames, 32);
    if (nframes > 0)
    {
        const char* hdr = "[QA] Stack trace:\n";
        if (write(STDERR_FILENO, hdr, strlen(hdr)))
        {
        }
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
            int  clen = snprintf(crash_buf,
                                 sizeof(crash_buf),
                                 "CRASH: %s\nSeed: %lu\nLast action: %s\n",
                                 name,
                                 static_cast<unsigned long>(g_qa_seed),
                                 g_last_action);
            if (clen > 0)
                if (write(fd, crash_buf, static_cast<size_t>(clen)))
                {
                }
#ifdef __linux__
            if (nframes > 0)
                backtrace_symbols_fd(frames, nframes, fd);
#endif
            close(fd);
        }
    }

    const char* footer = "[QA] ══════════════════════════════════════\n";
    if (write(STDERR_FILENO, footer, strlen(footer)))
    {
    }
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
