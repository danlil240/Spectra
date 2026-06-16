#pragma once
#ifdef SPECTRA_USE_IMGUI

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

    virtual void draw();

   protected:
    virtual void draw_empty_state() {}
    virtual void draw_overlays() {}

    spectra::LayoutManager* layout_manager_ = nullptr;
};
}   // namespace spectra::ui::shell
#endif   // SPECTRA_USE_IMGUI
