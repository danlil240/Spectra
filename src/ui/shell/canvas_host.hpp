#pragma once
#ifdef SPECTRA_USE_IMGUI

    #include <spectra/series.hpp>

namespace spectra
{
class LayoutManager;
}

namespace spectra::ui::shell
{
class CanvasHost
{
   public:
    explicit CanvasHost(spectra::LayoutManager* layout_manager = nullptr);
    virtual ~CanvasHost() = default;

    void                    set_layout_manager(spectra::LayoutManager* lm);
    spectra::LayoutManager* layout_manager() const;

    // Restrict the canvas to a sub-rectangle of the host window (screen space),
    // e.g. the central node of a dockspace so docked panels never cover the plot.
    // Without a region the whole host window content area is used.
    void set_region(const spectra::Rect& r);
    void clear_region();
    bool has_region() const { return region_valid_; }

    virtual void draw();

   protected:
    virtual void draw_empty_state() {}
    virtual void draw_overlays() {}

    // Screen-space rect the canvas occupied during the last draw().
    const spectra::Rect& canvas_rect() const { return canvas_rect_; }

    spectra::LayoutManager* layout_manager_ = nullptr;

   private:
    spectra::Rect region_{};
    spectra::Rect canvas_rect_{};
    bool          region_valid_ = false;
};
}   // namespace spectra::ui::shell
#endif   // SPECTRA_USE_IMGUI
