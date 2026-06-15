#pragma once
#ifdef SPECTRA_USE_IMGUI
    #include <functional>
    #include <vector>

namespace spectra
{
class LayoutManager;
}

namespace spectra::ui::shell
{
enum class StatusAlign
{
    Left,
    Center,
    Right
};

struct StatusSegment
{
    StatusAlign           align = StatusAlign::Left;
    std::function<void()> draw_fn;
};

class StatusBar
{
   public:
    void                    set_layout_manager(spectra::LayoutManager* lm);
    spectra::LayoutManager* layout_manager() const;

    void add_segment(StatusSegment segment);
    void clear();
    void draw();

   private:
    spectra::LayoutManager*    layout_manager_ = nullptr;
    std::vector<StatusSegment> segments_;
};
}   // namespace spectra::ui::shell
#endif   // SPECTRA_USE_IMGUI
