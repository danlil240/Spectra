// C FFI for 2D text annotations (spectra_axes_text / spectra_axes_annotate ...).
// Surface-backed checks skip when no Vulkan device is available.

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include <spectra/axes.hpp>
#include <spectra/spectra_embed_c.h>

namespace
{
struct EmbedDeleter
{
    void operator()(SpectraEmbed* s) const { spectra_embed_destroy(s); }
};
using EmbedPtr = std::unique_ptr<SpectraEmbed, EmbedDeleter>;
}   // namespace

TEST(EmbedAnnotationsC, NullHandlesAreSafe)
{
    EXPECT_EQ(spectra_axes_text(nullptr, 0.0f, 0.0f, "x"), -1);
    EXPECT_EQ(spectra_axes_annotate(nullptr, "x", 0.0f, 0.0f, 1.0f, 1.0f), -1);
    EXPECT_EQ(spectra_axes_annotation_count(nullptr), 0);
    spectra_axes_annotation_set_color(nullptr, 0, 1.0f, 0.0f, 0.0f, 1.0f);
    spectra_axes_annotation_set_font_size(nullptr, 0, 12.0f);
    spectra_axes_annotation_set_align(nullptr, 0, SPECTRA_HALIGN_RIGHT, SPECTRA_VALIGN_TOP);
    spectra_axes_annotation_set_visible(nullptr, 0, 0);
    spectra_axes_clear_annotations(nullptr);
}

TEST(EmbedAnnotationsC, TextAnnotateStyleAndClear)
{
    EmbedPtr s(spectra_embed_create(160, 120));
    if (!s)
        GTEST_SKIP() << "No Vulkan device";
    SpectraFigure* fig = spectra_embed_figure(s.get());
    ASSERT_NE(fig, nullptr);
    SpectraAxes* ax = spectra_figure_subplot(fig, 1, 1, 1);
    ASSERT_NE(ax, nullptr);

    const float x[3] = {0.0f, 1.0f, 2.0f};
    const float y[3] = {0.0f, 1.0f, 0.0f};
    ASSERT_NE(spectra_axes_line(ax, x, y, 3, nullptr), nullptr);

    EXPECT_EQ(spectra_axes_text(ax, 0.5f, 0.5f, nullptr), -1);
    EXPECT_EQ(spectra_axes_text(ax, 0.5f, 0.5f, "hello"), 0);
    EXPECT_EQ(spectra_axes_annotate(ax, "peak", 1.0f, 1.0f, 1.5f, 0.8f), 1);
    EXPECT_EQ(spectra_axes_annotation_count(ax), 2);

    spectra_axes_annotation_set_color(ax, 0, 1.0f, 0.0f, 0.0f, 1.0f);
    spectra_axes_annotation_set_font_size(ax, 0, 18.0f);
    spectra_axes_annotation_set_align(ax, 0, SPECTRA_HALIGN_RIGHT, SPECTRA_VALIGN_TOP);
    spectra_axes_annotation_set_visible(ax, 1, 0);
    spectra_axes_annotation_set_font_size(ax, 7, 30.0f);   // out of range: ignored

    // The handle wraps a spectra::Axes; inspect it through the C++ API.
    std::vector<uint8_t> buf(160 * 120 * 4);
    EXPECT_EQ(spectra_embed_render(s.get(), buf.data()), 1);

    EXPECT_EQ(spectra_axes_annotation_count(ax), 2);
    spectra_axes_clear_annotations(ax);
    EXPECT_EQ(spectra_axes_annotation_count(ax), 0);
    EXPECT_EQ(spectra_embed_render(s.get(), buf.data()), 1);
}
