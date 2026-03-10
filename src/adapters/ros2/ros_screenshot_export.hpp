#pragma once

// RosScreenshotExport — Screenshot and video export for the ROS2 adapter (E3).
//
// Provides two features:
//
//   1. Screenshot (Ctrl+Shift+S):
//      Captures the current plot-area RGBA buffer and writes a PNG to disk.
//      A user-supplied FrameGrabCallback provides the pixel data so this class
//      stays decoupled from the Vulkan backend.
//
//   2. Video export dialog (Tools → Record):
//      Thin ImGui wrapper around the core RecordingSession.  The caller wires
//      a FrameRenderCallback (same signature as RecordingSession) that is
//      invoked per frame during recording.  The dialog handles path/fps/format
//      input, progress display, and cancel.
//
// ScreenshotConfig ─ controls screenshot output.
// RecordingDialogConfig ─ controls default values shown in the dialog.
// RosScreenshotExport ─ main class; owns all mutable state.
//
// Usage (screenshot):
//   RosScreenshotExport exporter;
//   exporter.set_frame_grab_callback([&](uint8_t* buf, uint32_t w, uint32_t h){
//       my_vulkan_readback(buf, w, h);
//       return true;
//   });
//   exporter.take_screenshot("/tmp/plot.png", 1280, 720);
//
// Usage (recording dialog):
//   exporter.set_frame_render_callback([&](uint32_t fi, float t,
//                                          uint8_t* buf, uint32_t w, uint32_t h){
//       my_render_frame_to_buffer(buf, w, h);
//       return true;
//   });
//   // In ImGui frame:
//   exporter.draw_record_dialog(&show_record);
//
// Thread-safety:
//   All methods must be called from the render / ImGui thread.
//   The underlying RecordingSession is internally mutex-guarded.
//
// Dependencies:
//   - stb_image_write (included transitively via recording_export.cpp pattern)
//   - spectra::RecordingSession (src/ui/animation/recording_export.hpp)
//   - ImGui (draw_record_dialog gated on SPECTRA_USE_IMGUI)

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace spectra
{
class RecordingSession;
}

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// ScreenshotConfig
// ---------------------------------------------------------------------------

struct ScreenshotConfig
{
    // Output file path (must end with .png).
    std::string path;

    // Pixel dimensions of the captured image.
    uint32_t width  = 1280;
    uint32_t height = 720;
};

// ---------------------------------------------------------------------------
// ScreenshotResult
// ---------------------------------------------------------------------------

struct ScreenshotResult
{
    bool        ok = false;
    std::string path;   // Actual output path written
    uint32_t    width  = 0;
    uint32_t    height = 0;
    std::string error;   // Non-empty on failure

    explicit operator bool() const { return ok; }
};

// ---------------------------------------------------------------------------
// RecordingDialogConfig — defaults shown in the ImGui recording dialog
// ---------------------------------------------------------------------------

struct RecordingDialogConfig
{
    // Default output path template (strftime-style or plain path).
    std::string default_path = "/tmp/spectra_ros_recording.mp4";

    // Default FPS shown in the dialog.
    float default_fps = 30.0f;

    // Default recording duration in seconds.
    float default_duration = 10.0f;

    // Default output width / height.
    uint32_t default_width  = 1280;
    uint32_t default_height = 720;
};

// ---------------------------------------------------------------------------
// Callback types
// ---------------------------------------------------------------------------

// Called by take_screenshot() to obtain RGBA pixel data.
// buf   — caller-allocated buffer: width * height * 4 bytes (RGBA8)
// Returns true on success.
using FrameGrabCallback = std::function<bool(uint8_t* buf, uint32_t width, uint32_t height)>;

// Called by RecordingSession per frame during video recording.
// frame_index — 0-based frame counter
// time        — time in seconds for this frame
// buf         — caller-allocated buffer: width * height * 4 bytes (RGBA8)
// Returns true to continue, false to abort.
using FrameRenderCallback = std::function<
    bool(uint32_t frame_index, float time, uint8_t* buf, uint32_t width, uint32_t height)>;

// Called to resolve the current capture size for auto-sized screenshot/video
// export. Returns true when a live size is available.
using CaptureSizeCallback = std::function<bool(uint32_t& width, uint32_t& height)>;

// ---------------------------------------------------------------------------
// RosScreenshotExport — main class
// ---------------------------------------------------------------------------

class RosScreenshotExport
{
   public:
    explicit RosScreenshotExport(const RecordingDialogConfig& cfg = {});
    ~RosScreenshotExport();

    // Non-copyable, non-movable.
    RosScreenshotExport(const RosScreenshotExport&)            = delete;
    RosScreenshotExport& operator=(const RosScreenshotExport&) = delete;
    RosScreenshotExport(RosScreenshotExport&&)                 = delete;
    RosScreenshotExport& operator=(RosScreenshotExport&&)      = delete;

    // -----------------------------------------------------------------------
    // Callbacks
    // -----------------------------------------------------------------------

    // Set the callback invoked during take_screenshot() to obtain pixel data.
    void set_frame_grab_callback(FrameGrabCallback cb);

    // Set the callback invoked per frame during RecordingSession video export.
    void set_frame_render_callback(FrameRenderCallback cb);

    // Set the callback used to resolve the current live capture size.
    void set_capture_size_callback(CaptureSizeCallback cb);

    // -----------------------------------------------------------------------
    // Screenshot
    // -----------------------------------------------------------------------

    // Capture and write a single PNG screenshot.
    // Uses the registered FrameGrabCallback to fill the pixel buffer.
    // Returns a ScreenshotResult describing success or failure.
    ScreenshotResult take_screenshot(const std::string& path,
                                     uint32_t           width  = 0,
                                     uint32_t           height = 0);

    // Overload using a fully populated ScreenshotConfig.
    ScreenshotResult take_screenshot(const ScreenshotConfig& cfg);

    // Static helper: write an RGBA8 buffer to a PNG file.
    // Returns true on success.
    static bool write_png(const std::string& path,
                          const uint8_t*     rgba,
                          uint32_t           width,
                          uint32_t           height);

    // Static helper: generate a timestamped screenshot filename.
    // Format: <prefix>_YYYYMMDD_HHMMSS.png
    static std::string make_screenshot_path(const std::string& dir    = "/tmp",
                                            const std::string& prefix = "spectra_ros");

    // -----------------------------------------------------------------------
    // Last screenshot result
    // -----------------------------------------------------------------------

    const ScreenshotResult& last_screenshot() const { return last_screenshot_; }

    // -----------------------------------------------------------------------
    // ImGui recording dialog
    // -----------------------------------------------------------------------

    // Draw the recording dialog window.
    // p_open — ImGui boolean controlling window visibility (may be nullptr).
    // Returns true while the dialog is open.
    bool draw_record_dialog(bool* p_open = nullptr);

    // True while a recording session is active.
    bool is_recording() const;

    // True after the last recording session completed successfully.
    bool last_record_ok() const { return last_record_ok_; }

    // Cancel any active recording session.
    void cancel_recording();

    // -----------------------------------------------------------------------
    // Dialog config accessors
    // -----------------------------------------------------------------------

    const RecordingDialogConfig& dialog_config() const { return dialog_cfg_; }
    void set_dialog_config(const RecordingDialogConfig& cfg) { dialog_cfg_ = cfg; }

    // -----------------------------------------------------------------------
    // Screenshot auto-path state (for UI status display)
    // -----------------------------------------------------------------------

    // Returns the path of the most recently written screenshot (empty if none).
    const std::string& last_screenshot_path() const { return last_screenshot_.path; }

    // Resolve the capture size currently used for auto-sized exports.
    bool current_capture_size(uint32_t& width, uint32_t& height) const;

    // True if a screenshot toast notification should still be shown.
    // Resets after toast_duration_s (default 3 s) or on next take_screenshot().
    bool screenshot_toast_active() const;

    // Advance the toast timer by dt seconds (call once per frame).
    void tick(float dt);

   private:
    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------

    void begin_recording_from_dialog();
    void reset_recording_timing_state();

    // -----------------------------------------------------------------------
    // Members
    // -----------------------------------------------------------------------

    RecordingDialogConfig dialog_cfg_;

    FrameGrabCallback   grab_cb_;
    FrameRenderCallback render_cb_;
    CaptureSizeCallback capture_size_cb_;

    // Screenshot state.
    ScreenshotResult last_screenshot_;
    float            toast_timer_s_    = 0.0f;
    float            toast_duration_s_ = 3.0f;

    // Recording session (owned, heap-allocated to avoid heavy header in .hpp).
    std::unique_ptr<spectra::RecordingSession> session_;
    bool                                       last_record_ok_ = false;

    // Live ROS recording is driven by wall-clock ticks rather than by the UI
    // repaint rate. When multiple output frames are due in one poll, reuse the
    // latest captured framebuffer instead of forcing repeated readbacks.
    std::vector<uint8_t> recording_frame_cache_;
    float                recording_accumulator_s_      = 0.0f;
    uint64_t             recording_capture_generation_ = 0;
    uint64_t             recording_cached_generation_  = 0;
    bool                 recording_cache_valid_        = false;

    // Dialog input buffers.
    // Sized for ImGui::InputText calls (no dynamic allocation per frame).
    static constexpr int kPathBuf = 512;

    char  dialog_path_buf_[kPathBuf];
    float dialog_fps_         = 30.0f;
    float dialog_duration_s_  = 10.0f;
    int   dialog_format_idx_  = 0;   // 0=PNG_Sequence 1=GIF 2=MP4
    bool  dialog_initialized_ = false;
};

}   // namespace spectra::adapters::ros2
