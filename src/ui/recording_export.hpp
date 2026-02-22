#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <spectra/color.hpp>
#include <spectra/fwd.hpp>
#include <string>
#include <vector>

namespace spectra
{

// Output format for recording export.
enum class RecordingFormat
{
    PNG_Sequence,   // Individual PNG frames in a directory
    GIF,            // Animated GIF (via stb_image_write)
    MP4,            // MP4 via ffmpeg pipe (requires SPECTRA_USE_FFMPEG)
};

// Recording quality preset.
enum class RecordingQuality
{
    Draft,    // Lower resolution, faster encoding
    Normal,   // Standard quality
    High,     // High quality, larger files
};

// Configuration for a recording session.
struct RecordingConfig
{
    RecordingFormat  format  = RecordingFormat::PNG_Sequence;
    RecordingQuality quality = RecordingQuality::Normal;

    std::string output_path;   // File path or directory for PNG sequence
    uint32_t    width      = 1280;
    uint32_t    height     = 720;
    float       fps        = 60.0f;
    float       start_time = 0.0f;
    float       end_time   = 0.0f;   // 0 = use timeline duration

    // GIF-specific
    uint32_t gif_palette_size = 256;   // Max colors in GIF palette
    bool     gif_dither       = true;

    // MP4-specific
    std::string codec   = "libx264";
    std::string pix_fmt = "yuv420p";
    int         crf     = 23;   // Constant rate factor (lower = better quality)

    // Multi-pane recording (Week 11 enhancement)
    // When pane_count > 1, the render callback is called once per pane per frame.
    // Panes are composited into the final frame buffer according to the layout.
    uint32_t pane_count = 1;

    // Pane layout: each pane has normalized [0,1] rect within the output frame.
    struct PaneRect
    {
        float x = 0.0f;
        float y = 0.0f;
        float w = 1.0f;
        float h = 1.0f;
    };
    std::vector<PaneRect> pane_rects;   // If empty and pane_count>1, auto-grid layout
};

// Progress information for recording callbacks.
struct RecordingProgress
{
    uint32_t current_frame           = 0;
    uint32_t total_frames            = 0;
    float    elapsed_sec             = 0.0f;
    float    estimated_remaining_sec = 0.0f;
    float    percent                 = 0.0f;
    bool     cancelled               = false;
};

// Recording session state.
enum class RecordingState
{
    Idle,
    Preparing,
    Recording,
    Encoding,
    Finished,
    Failed,
    Cancelled,
};

// Callback types.
using ProgressCallback    = std::function<void(const RecordingProgress&)>;
using FrameRenderCallback = std::function<
    bool(uint32_t frame_index, float time, uint8_t* rgba_buffer, uint32_t width, uint32_t height)>;

// Multi-pane render callback: receives pane_index in addition to frame info.
using PaneRenderCallback = std::function<bool(uint32_t pane_index,
                                              uint32_t frame_index,
                                              float    time,
                                              uint8_t* rgba_buffer,
                                              uint32_t width,
                                              uint32_t height)>;

// RecordingSession — Orchestrates frame-by-frame recording and export.
//
// Usage:
//   1. Create a RecordingConfig
//   2. Call begin() with the config and a frame render callback
//   3. Call advance() each frame (or run_all() for batch)
//   4. Call finish() when done (or cancel() to abort)
//
// The frame render callback is responsible for rendering each frame into
// the provided RGBA buffer. This decouples recording from the rendering
// pipeline — the session doesn't need to know about Vulkan/OpenGL.
//
// Thread-safe: all public methods lock an internal mutex.
class RecordingSession
{
   public:
    RecordingSession();
    ~RecordingSession();

    RecordingSession(const RecordingSession&)            = delete;
    RecordingSession& operator=(const RecordingSession&) = delete;

    // ─── Session lifecycle ───────────────────────────────────────────────

    // Begin a recording session. Returns false if config is invalid.
    bool begin(const RecordingConfig& config, FrameRenderCallback render_cb);

    // Begin a multi-pane recording session. Each pane is rendered separately
    // and composited into the final frame buffer.
    bool begin_multi_pane(const RecordingConfig& config, PaneRenderCallback pane_cb);

    // Advance one frame. Returns true if more frames remain.
    bool advance();

    // Run all remaining frames in a blocking loop.
    bool run_all();

    // Finish the recording (finalize encoding, write output).
    bool finish();

    // Cancel the recording.
    void cancel();

    // ─── State queries ───────────────────────────────────────────────────

    RecordingState state() const;
    bool           is_active() const;
    bool           is_finished() const;

    const RecordingConfig& config() const;
    RecordingProgress      progress() const;

    // Error message if state is Failed.
    std::string error() const;

    // ─── Frame data ──────────────────────────────────────────────────────

    // Total number of frames to record.
    uint32_t total_frames() const;

    // Current frame index (0-based).
    uint32_t current_frame() const;

    // Time in seconds for a given frame index.
    float frame_time(uint32_t frame_index) const;

    // ─── Callbacks ───────────────────────────────────────────────────────

    void set_on_progress(ProgressCallback cb);
    void set_on_complete(std::function<void(bool success)> cb);

    // ─── GIF utilities (static) ──────────────────────────────────────────

    // Quantize an RGBA image to a palette of at most max_colors.
    // Returns palette (max_colors * 3 bytes RGB) and indexed image.
    static void quantize_frame(const uint8_t*        rgba,
                               uint32_t              width,
                               uint32_t              height,
                               uint32_t              max_colors,
                               std::vector<uint8_t>& palette,
                               std::vector<uint8_t>& indexed);

    // Compute median-cut color quantization on a set of RGBA pixels.
    static std::vector<Color> median_cut(const uint8_t* rgba,
                                         size_t         pixel_count,
                                         uint32_t       max_colors);

    // Find nearest palette index for a given color.
    static uint8_t nearest_palette_index(const std::vector<Color>& palette,
                                         uint8_t                   r,
                                         uint8_t                   g,
                                         uint8_t                   b);

   private:
    mutable std::mutex mutex_;

    RecordingConfig config_;
    RecordingState  state_ = RecordingState::Idle;
    std::string     error_;

    FrameRenderCallback       render_cb_;
    PaneRenderCallback        pane_render_cb_;
    ProgressCallback          on_progress_;
    std::function<void(bool)> on_complete_;

    // Multi-pane state
    bool                 multi_pane_ = false;
    std::vector<uint8_t> pane_buffer_;   // Temp buffer for individual pane rendering
    std::vector<RecordingConfig::PaneRect> resolved_pane_rects_;

    uint32_t total_frames_  = 0;
    uint32_t current_frame_ = 0;

    // Frame buffer (RGBA, width * height * 4 bytes)
    std::vector<uint8_t> frame_buffer_;

    // Timing
    float start_wall_time_ = 0.0f;

    // PNG sequence state
    uint32_t png_frame_digits_ = 4;

    // GIF accumulation state
    struct GifState
    {
        std::vector<std::vector<uint8_t>> frames;           // Indexed frames
        std::vector<uint8_t>              global_palette;   // RGB palette
        bool                              palette_computed = false;
    };
    std::unique_ptr<GifState> gif_state_;

    // MP4 pipe state (only when SPECTRA_USE_FFMPEG is enabled)
#ifdef SPECTRA_USE_FFMPEG
    FILE* ffmpeg_pipe_ = nullptr;
#endif

    // Internal helpers
    bool  validate_config() const;
    bool  prepare_output();
    bool  write_png_frame();
    bool  accumulate_gif_frame();
    bool  write_gif();
    bool  write_mp4_frame();
    bool  finalize_mp4();
    void  update_progress();
    void  set_error(const std::string& msg);
    float wall_time() const;
};

}   // namespace spectra
