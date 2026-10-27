// Plot text at arbitrary pixel sizes (issue #109): annotation font sizes and shrunk
// titles are drawn at their requested size (a baked preset scaled), not snapped to one
// of the three baked presets.  GPU (headless export); label `gpu`.

#include <gtest/gtest.h>

#include <spectra/app.hpp>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "stb_image.h"

namespace fs = std::filesystem;

namespace
{

struct Image
{
    int                  w = 0;
    int                  h = 0;
    std::vector<uint8_t> rgba;
};

bool load(const fs::path& p, Image& img)
{
    int      ch   = 0;
    stbi_uc* data = stbi_load(p.string().c_str(), &img.w, &img.h, &ch, 4);
    if (!data)
        return false;
    img.rgba.assign(data, data + static_cast<size_t>(img.w) * img.h * 4);
    stbi_image_free(data);
    return true;
}

struct InkBox
{
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;
    int width() const { return x1 >= x0 ? x1 - x0 + 1 : 0; }
    int height() const { return y1 >= y0 ? y1 - y0 + 1 : 0; }
};

// Bounding box of the pixels that differ clearly between two same-sized images.
InkBox diff_box(const Image& a, const Image& b)
{
    InkBox box;
    for (int y = 0; y < a.h; ++y)
        for (int x = 0; x < a.w; ++x)
        {
            const size_t   i = (static_cast<size_t>(y) * a.w + x) * 4;
            const uint8_t* p = &a.rgba[i];
            const uint8_t* q = &b.rgba[i];
            const int d = std::abs(p[0] - q[0]) + std::abs(p[1] - q[1]) + std::abs(p[2] - q[2]);
            if (d > 90)
            {
                box.x0 = std::min(box.x0, x);
                box.y0 = std::min(box.y0, y);
                box.x1 = std::max(box.x1, x);
                box.y1 = std::max(box.y1, y);
            }
        }
    return box;
}

void plot(spectra::Axes& ax)
{
    std::vector<float> x{0.0f, 10.0f}, y{0.0f, 0.0f};
    ax.plot(x, y, "b-");
    ax.xlim(0.0, 10.0);
    ax.ylim(-1.0, 1.0);
}

}   // namespace

// Requested annotation sizes 20, 24 and 28 px all used to snap to the 18 px Title
// preset (identical text); they are now drawn at their own size.
TEST(TextScaling, AnnotationFontSizeIsHonouredExactly)
{
    const fs::path dir = fs::temp_directory_path() / "spectra_test_text_scaling";
    fs::create_directories(dir);
    constexpr uint32_t W = 640, H = 400;
    const float        sizes[] = {14.0f, 20.0f, 24.0f, 28.0f};
    {
        spectra::App app({.headless = true, .socket_path = ""});
        auto&        base = app.figure({.width = W, .height = H});
        plot(base.subplot(1, 1, 1));
        base.save_png((dir / "base.png").string());
        for (float s : sizes)
        {
            auto& fig = app.figure({.width = W, .height = H});
            auto& ax  = fig.subplot(1, 1, 1);
            plot(ax);
            ax.text(5.0f, 0.5f, "HHHHHHHH").font_size(s);
            fig.save_png((dir / ("ann_" + std::to_string(static_cast<int>(s)) + ".png")).string());
        }
        app.run();
    }

    Image base;
    ASSERT_TRUE(load(dir / "base.png", base));
    std::vector<InkBox> boxes;
    for (float s : sizes)
    {
        Image img;
        ASSERT_TRUE(load(dir / ("ann_" + std::to_string(static_cast<int>(s)) + ".png"), img));
        ASSERT_EQ(img.w, base.w);
        boxes.push_back(diff_box(img, base));
        ASSERT_GT(boxes.back().width(), 0) << "annotation at " << s << "px not drawn";
    }

    // Text width is proportional to the requested size (within a few pixels of
    // anti-aliasing), so 20/24/28 px give clearly different widths.
    const float w14 = static_cast<float>(boxes[0].width());
    for (size_t i = 1; i < boxes.size(); ++i)
    {
        const float expected = w14 * sizes[i] / sizes[0];
        EXPECT_NEAR(static_cast<float>(boxes[i].width()), expected, 0.04f * expected + 3.0f)
            << sizes[i] << "px";
        EXPECT_GT(boxes[i].width(), boxes[i - 1].width() + 4) << sizes[i] << "px";
        EXPECT_GT(boxes[i].height(), boxes[i - 1].height()) << sizes[i] << "px";
    }
    fs::remove_all(dir);
}
