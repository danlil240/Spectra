#include "ros_screenshot_export.hpp"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>

// stb_image_write — PNG screenshot export.
// We define STB_IMAGE_WRITE_STATIC + STB_IMAGE_WRITE_IMPLEMENTATION here so
// this translation unit owns the implementation independently of the core
// library's stb_impl.cpp (which is not linked into spectra_ros2_adapter).
// third_party/stb is added to spectra_ros2_adapter include dirs in CMakeLists.
#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
    #pragma clang diagnostic ignored "-Wsign-conversion"
    #pragma clang diagnostic ignored "-Wimplicit-fallthrough"
    #pragma clang diagnostic ignored "-Wdouble-promotion"
    #pragma clang diagnostic ignored "-Wmissing-prototypes"
    #pragma clang diagnostic ignored "-Wcast-qual"
#elif defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-function"
    #pragma GCC diagnostic ignored "-Wsign-conversion"
    #pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
    #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#if defined(__clang__)
    #pragma clang diagnostic pop
#elif defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

// RecordingSession — full video export capability.
// Include path resolves via the PRIVATE src/ include dir of spectra_ros2_adapter.
#include "ui/animation/recording_export.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
#endif

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

RosScreenshotExport::RosScreenshotExport(const RecordingDialogConfig& cfg)
    : dialog_cfg_(cfg)
    , dialog_fps_(cfg.default_fps)
    , dialog_duration_s_(cfg.default_duration)
    , dialog_width_(static_cast<int>(cfg.default_width))
    , dialog_height_(static_cast<int>(cfg.default_height))
{
    std::memset(dialog_path_buf_, 0, sizeof(dialog_path_buf_));
    std::strncpy(dialog_path_buf_, cfg.default_path.c_str(), kPathBuf - 1);
}

RosScreenshotExport::~RosScreenshotExport()
{
    if (session_ && session_->is_active())
        session_->cancel();
}

// ---------------------------------------------------------------------------
// Callback setters
// ---------------------------------------------------------------------------

void RosScreenshotExport::set_frame_grab_callback(FrameGrabCallback cb)
{
    grab_cb_ = std::move(cb);
}

void RosScreenshotExport::set_frame_render_callback(FrameRenderCallback cb)
{
    render_cb_ = std::move(cb);
}

// ---------------------------------------------------------------------------
// Screenshot
// ---------------------------------------------------------------------------

ScreenshotResult RosScreenshotExport::take_screenshot(const ScreenshotConfig& cfg)
{
    ScreenshotResult result;
    result.path   = cfg.path;
    result.width  = cfg.width;
    result.height = cfg.height;

    // Reset toast.
    toast_timer_s_ = 0.0f;

    if (cfg.path.empty())
    {
        result.error = "Screenshot path is empty";
        last_screenshot_ = result;
        return result;
    }
    if (cfg.width == 0 || cfg.height == 0)
    {
        result.error = "Screenshot dimensions must be > 0";
        last_screenshot_ = result;
        return result;
    }

    // Allocate pixel buffer.
    std::vector<uint8_t> buf(static_cast<size_t>(cfg.width) *
                             static_cast<size_t>(cfg.height) * 4u, 0u);

    // Invoke grab callback to fill pixels.
    if (grab_cb_)
    {
        if (!grab_cb_(buf.data(), cfg.width, cfg.height))
        {
            result.error = "Frame grab callback returned false";
            last_screenshot_ = result;
            return result;
        }
    }
    // If no grab callback, the buffer stays zero (black image) — valid for testing.

    // Write PNG.
    if (!write_png(cfg.path, buf.data(), cfg.width, cfg.height))
    {
        result.error = "stb_image_write failed for path: " + cfg.path;
        last_screenshot_ = result;
        return result;
    }

    result.ok = true;
    toast_timer_s_ = toast_duration_s_;
    last_screenshot_ = result;
    return result;
}

ScreenshotResult RosScreenshotExport::take_screenshot(const std::string& path,
                                                       uint32_t width,
                                                       uint32_t height)
{
    ScreenshotConfig cfg;
    cfg.path   = path;
    cfg.width  = width;
    cfg.height = height;
    return take_screenshot(cfg);
}

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

bool RosScreenshotExport::write_png(const std::string& path,
                                    const uint8_t* rgba,
                                    uint32_t width,
                                    uint32_t height)
{
    if (!rgba || width == 0 || height == 0 || path.empty())
        return false;

    const int stride = static_cast<int>(width) * 4;
    const int ret    = stbi_write_png(path.c_str(),
                                      static_cast<int>(width),
                                      static_cast<int>(height),
                                      4,
                                      rgba,
                                      stride);
    return ret != 0;
}

std::string RosScreenshotExport::make_screenshot_path(const std::string& dir,
                                                       const std::string& prefix)
{
    const auto now    = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &tt);
#else
    localtime_r(&tt, &tm_buf);
#endif

    char date_buf[32];
    std::strftime(date_buf, sizeof(date_buf), "%Y%m%d_%H%M%S", &tm_buf);

    std::string result = dir;
    if (!result.empty() && result.back() != '/')
        result += '/';
    result += prefix + '_' + date_buf + ".png";
    return result;
}

// ---------------------------------------------------------------------------
// Toast timer
// ---------------------------------------------------------------------------

bool RosScreenshotExport::screenshot_toast_active() const
{
    return toast_timer_s_ > 0.0f;
}

void RosScreenshotExport::tick(float dt)
{
    if (toast_timer_s_ > 0.0f)
    {
        toast_timer_s_ -= dt;
        if (toast_timer_s_ < 0.0f) toast_timer_s_ = 0.0f;
    }

    // Advance recording session (non-blocking: advance() should be called
    // from the render loop, but we provide a convenience tick here for
    // headless usage and shell integration).
    if (session_ && session_->is_active())
    {
        if (!session_->advance())
        {
            // No more frames — finalize.
            last_record_ok_ = session_->finish();
        }
    }
}

// ---------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------

bool RosScreenshotExport::is_recording() const
{
    return session_ && session_->is_active();
}

void RosScreenshotExport::cancel_recording()
{
    if (session_ && session_->is_active())
    {
        session_->cancel();
        last_record_ok_ = false;
    }
}

void RosScreenshotExport::begin_recording_from_dialog()
{
    if (!render_cb_) return;

    // Build RecordingConfig from dialog state.
    spectra::RecordingConfig cfg;
    cfg.output_path = std::string(dialog_path_buf_);
    cfg.fps         = dialog_fps_;
    cfg.end_time    = dialog_duration_s_;
    cfg.width       = static_cast<uint32_t>(dialog_width_);
    cfg.height      = static_cast<uint32_t>(dialog_height_);

    switch (dialog_format_idx_)
    {
        case 0: cfg.format = spectra::RecordingFormat::PNG_Sequence; break;
        case 1: cfg.format = spectra::RecordingFormat::GIF;          break;
        default:
        case 2: cfg.format = spectra::RecordingFormat::MP4;          break;
    }

    // Build FrameRenderCallback bridging our adapter callback signature.
    FrameRenderCallback local_cb = render_cb_;
    spectra::FrameRenderCallback session_cb =
        [local_cb](uint32_t fi, float t, uint8_t* buf, uint32_t w, uint32_t h) -> bool
        {
            return local_cb(fi, t, buf, w, h);
        };

    if (!session_)
        session_ = std::make_unique<spectra::RecordingSession>();

    last_record_ok_ = false;
    if (!session_->begin(cfg, session_cb))
    {
        // begin() failed — session remains Idle/Failed.
    }
}

// ---------------------------------------------------------------------------
// ImGui recording dialog
// ---------------------------------------------------------------------------

bool RosScreenshotExport::draw_record_dialog(bool* p_open)
{
#ifdef SPECTRA_USE_IMGUI
    if (!ImGui::GetCurrentContext()) return false;

    // Initialize dialog buffers from config on first open.
    if (!dialog_initialized_)
    {
        dialog_initialized_ = true;
        std::memset(dialog_path_buf_, 0, sizeof(dialog_path_buf_));
        std::strncpy(dialog_path_buf_, dialog_cfg_.default_path.c_str(), kPathBuf - 1);
        dialog_fps_        = dialog_cfg_.default_fps;
        dialog_duration_s_ = dialog_cfg_.default_duration;
        dialog_width_      = static_cast<int>(dialog_cfg_.default_width);
        dialog_height_     = static_cast<int>(dialog_cfg_.default_height);
    }

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_AlwaysAutoResize;
    bool open = true;
    if (!ImGui::Begin("Record Video##spectra_ros_record", p_open ? p_open : &open, flags))
    {
        ImGui::End();
        return false;
    }

    const bool recording_now = is_recording();

    // ── Output path ──────────────────────────────────────────────────────────
    ImGui::Text("Output path:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(320.0f);
    ImGui::BeginDisabled(recording_now);
    ImGui::InputText("##rec_path", dialog_path_buf_, kPathBuf);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Auto##rec_autopath"))
    {
        const std::string auto_path = make_screenshot_path("/tmp", "spectra_ros_recording");
        // Change extension to match format.
        std::string ext;
        switch (dialog_format_idx_)
        {
            case 0: ext = "";         break;   // PNG sequence: no single extension
            case 1: ext = ".gif";     break;
            default:
            case 2: ext = ".mp4";     break;
        }
        std::string new_path = auto_path;
        if (!ext.empty())
        {
            // Replace .png suffix with the appropriate extension.
            const auto dot = new_path.rfind('.');
            if (dot != std::string::npos)
                new_path = new_path.substr(0, dot) + ext;
            else
                new_path += ext;
        }
        std::memset(dialog_path_buf_, 0, sizeof(dialog_path_buf_));
        std::strncpy(dialog_path_buf_, new_path.c_str(), kPathBuf - 1);
    }

    // ── Format ───────────────────────────────────────────────────────────────
    ImGui::BeginDisabled(recording_now);
    ImGui::Text("Format:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    const char* format_items[] = { "PNG Sequence", "GIF", "MP4 (ffmpeg)" };
    ImGui::Combo("##rec_fmt", &dialog_format_idx_, format_items, 3);
    ImGui::EndDisabled();

    // ── Settings ─────────────────────────────────────────────────────────────
    ImGui::BeginDisabled(recording_now);
    ImGui::Text("FPS:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputFloat("##rec_fps", &dialog_fps_, 1.0f, 5.0f, "%.1f");
    if (dialog_fps_ < 1.0f)  dialog_fps_ = 1.0f;
    if (dialog_fps_ > 240.0f) dialog_fps_ = 240.0f;

    ImGui::SameLine();
    ImGui::Text("  Duration (s):");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputFloat("##rec_dur", &dialog_duration_s_, 1.0f, 10.0f, "%.1f");
    if (dialog_duration_s_ < 0.1f)  dialog_duration_s_ = 0.1f;
    if (dialog_duration_s_ > 3600.0f) dialog_duration_s_ = 3600.0f;

    ImGui::Text("Width:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    ImGui::InputInt("##rec_w", &dialog_width_);
    if (dialog_width_ < 64)    dialog_width_ = 64;
    if (dialog_width_ > 7680)  dialog_width_ = 7680;

    ImGui::SameLine();
    ImGui::Text("  Height:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    ImGui::InputInt("##rec_h", &dialog_height_);
    if (dialog_height_ < 64)    dialog_height_ = 64;
    if (dialog_height_ > 4320)  dialog_height_ = 4320;

    ImGui::EndDisabled();

    // ── Progress ─────────────────────────────────────────────────────────────
    if (recording_now && session_)
    {
        const auto prog = session_->progress();
        ImGui::Separator();
        ImGui::Text("Recording: frame %u / %u", prog.current_frame, prog.total_frames);
        ImGui::ProgressBar(prog.percent / 100.0f, ImVec2(-1.0f, 0.0f));
        char eta_buf[64];
        std::snprintf(eta_buf, sizeof(eta_buf), "ETA: %.1f s",
                      static_cast<double>(prog.estimated_remaining_sec));
        ImGui::TextDisabled("%s", eta_buf);
    }

    // ── Completion status ─────────────────────────────────────────────────────
    if (session_ && session_->is_finished())
    {
        if (last_record_ok_)
        {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Done: %s",
                               dialog_path_buf_);
        }
        else
        {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Failed: %s",
                               session_->error().c_str());
        }
    }

    // ── Buttons ───────────────────────────────────────────────────────────────
    ImGui::Separator();

    if (!recording_now)
    {
        const bool can_record = render_cb_ && (dialog_path_buf_[0] != '\0');
        ImGui::BeginDisabled(!can_record);
        if (ImGui::Button("Start Recording##rec_start"))
            begin_recording_from_dialog();
        ImGui::EndDisabled();

        if (!can_record)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(set a render callback to enable recording)");
        }
    }
    else
    {
        if (ImGui::Button("Cancel##rec_cancel"))
            cancel_recording();
    }

    // ── Screenshot shortcut hint ──────────────────────────────────────────────
    ImGui::Separator();
    ImGui::TextDisabled("Tip: Ctrl+Shift+S takes an instant screenshot.");

    ImGui::End();
    return p_open ? *p_open : open;
#else
    (void)p_open;
    return false;
#endif
}

}   // namespace spectra::adapters::ros2
