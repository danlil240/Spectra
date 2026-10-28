#pragma once

// window_png_export.hpp — services Figure::save_png() / copy_to_clipboard()
// requests in windowed mode (inproc App::step and the multiproc window agent).
//
// A windowed PNG is read back from the swapchain image of a later frame, right
// before it is presented.  The window can change size between the request and
// that frame (minimum-size enforcement after the first frame, a user resize, a
// DPI change), so the capture size is resolved when the capture executes, from
// the image that frame actually rendered.  A default-sized export is written at
// that size; an explicitly sized export is scaled to the requested size
// (aspect preserved) instead of being cropped or padded.

#include <cstdint>
#include <string>
#include <vector>

namespace spectra
{

class Figure;
class FigureRegistry;
class VulkanBackend;
struct WindowContext;
class WindowManager;

namespace ui
{
class ThemeManager;
}

// Scale a tightly packed RGBA8 image of src_w x src_h into a dst_w x dst_h
// buffer.  The image is scaled uniformly to fit (area-averaged when shrinking,
// bilinear when enlarging) and centered; uncovered pixels are transparent
// (all zero).  @p dst must hold dst_w * dst_h * 4 bytes.
void fit_rgba_image(const uint8_t* src,
                    uint32_t       src_w,
                    uint32_t       src_h,
                    uint8_t*       dst,
                    uint32_t       dst_w,
                    uint32_t       dst_h);

class WindowPngExport
{
   public:
    // True while a request is scheduled or its capture is waiting to be written.
    bool active() const { return active_; }
    // True while the capture has been requested but not yet collected; the
    // next rendered frame is the one that is read back.
    bool awaiting_capture() const { return active_ && awaiting_gpu_capture_; }
    // Per-export theme of the scheduled capture (empty = app theme).
    const std::string& theme() const { return theme_; }

    // Write the finished capture of a previous frame (PNG file or clipboard).
    // Does nothing while the capture is still pending.  Call once per step,
    // after the frame has been rendered.
    void collect(VulkanBackend* vk);

    // Schedule a capture for the pending PNG / clipboard request of @p fig, if
    // any and none is active.  @p target_window is the window showing @p fig
    // (null = the next frame of any window).  Returns true when scheduled; the
    // figure's request is consumed either way once it is examined.
    bool schedule(Figure& fig, VulkanBackend* vk, WindowContext* target_window);

   private:
    void reset();

    std::string path_;
    std::string theme_;
    uint32_t    requested_width_      = 0;   // 0 = size of the rendered frame
    uint32_t    requested_height_     = 0;
    bool        active_               = false;
    bool        copy_to_clipboard_    = false;
    bool        awaiting_gpu_capture_ = false;
};

#if defined(SPECTRA_USE_GLFW) || defined(SPECTRA_USE_SDL3)
// The window whose assigned figures contain @p fig_id, or null.
WindowContext* find_window_for_figure(WindowManager* window_mgr, uint64_t fig_id);
#endif

// Write the pending SVG request of @p fig (if any) and clear it.  Returns false
// only when a write was attempted and failed.
bool write_pending_svg(Figure& fig, ui::ThemeManager* theme_mgr);

// One frame of windowed export servicing, shared by App::step() (inproc) and
// the multiproc window agent, called after the frame has been rendered:
//  - writes a finished PNG / clipboard capture,
//  - schedules a capture for the active figure's pending PNG / clipboard
//    request (aimed at the window showing it),
//  - writes every figure's pending SVG request right away.
// @p active_id may be INVALID / unknown (no PNG scheduled).  Returns true when
// a PNG capture was scheduled: the caller must make the next frame render.
bool service_window_exports(WindowPngExport&  png_export,
                            FigureRegistry&   registry,
                            uint64_t          active_id,
                            VulkanBackend*    vk,
                            WindowManager*    window_mgr,
                            ui::ThemeManager* theme_mgr);

}   // namespace spectra
