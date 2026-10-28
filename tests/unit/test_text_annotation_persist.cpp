// Text annotations (Axes::text / Axes::annotate) in saved files: the JSON
// workspace (capture / save / load / apply, with older files that lack the
// key) and the binary figure file (FigureSerializer).

#include <gtest/gtest.h>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "ui/workspace/figure_serializer.hpp"
#include "ui/workspace/workspace.hpp"

using namespace spectra;

namespace
{

std::string temp_path(const char* name)
{
    return (std::filesystem::temp_directory_path() / name).string();
}

void add_sample_annotations(Axes& ax)
{
    ax.line(std::vector<float>{0.0f, 1.0f, 2.0f}, std::vector<float>{0.0f, 1.0f, 0.0f});
    ax.text(0.5f, 0.25f, "plain {json} [chars] \"q\"");
    ax.annotate("peak", 1.0f, 1.0f, 1.5f, 1.4f)
        .color(Color(0.2f, 0.4f, 0.6f, 0.5f))
        .font_size(20.0f)
        .visible(false);
}

void expect_sample_annotations(const Axes& ax)
{
    const auto& anns = ax.annotations();
    ASSERT_EQ(anns.size(), 2u);
    EXPECT_EQ(anns[0]->text(), "plain {json} [chars] \"q\"");
    EXPECT_FLOAT_EQ(anns[0]->x(), 0.5f);
    EXPECT_FLOAT_EQ(anns[0]->y(), 0.25f);
    EXPECT_FALSE(anns[0]->color().has_value());
    EXPECT_FALSE(anns[0]->has_arrow());
    EXPECT_EQ(anns[0]->halign(), TextAnnotation::HAlign::Left);
    EXPECT_EQ(anns[0]->valign(), TextAnnotation::VAlign::Bottom);
    EXPECT_TRUE(anns[0]->visible());

    EXPECT_EQ(anns[1]->text(), "peak");
    EXPECT_FLOAT_EQ(anns[1]->x(), 1.5f);
    EXPECT_FLOAT_EQ(anns[1]->y(), 1.4f);
    ASSERT_TRUE(anns[1]->color().has_value());
    EXPECT_FLOAT_EQ(anns[1]->color()->b, 0.6f);
    EXPECT_FLOAT_EQ(anns[1]->color()->a, 0.5f);
    EXPECT_FLOAT_EQ(anns[1]->font_size(), 20.0f);
    EXPECT_TRUE(anns[1]->has_arrow());
    EXPECT_FLOAT_EQ(anns[1]->arrow_x(), 1.0f);
    EXPECT_FLOAT_EQ(anns[1]->arrow_y(), 1.0f);
    EXPECT_EQ(anns[1]->halign(), TextAnnotation::HAlign::Center);
    EXPECT_EQ(anns[1]->valign(), TextAnnotation::VAlign::Middle);
    EXPECT_FALSE(anns[1]->visible());
}

}   // namespace

TEST(TextAnnotationPersist, WorkspaceRoundTrip)
{
    Figure src;
    add_sample_annotations(src.subplot(1, 2, 1));
    src.subplot(1, 2, 2).text(3.0f, 4.0f, "second axes");

    std::vector<Figure*> figs = {&src};
    auto                 data = Workspace::capture(figs, 0, "dark", true, 320.0f, false);
    ASSERT_EQ(data.figures.size(), 1u);
    EXPECT_EQ(data.figures[0].text_annotations.size(), 3u);

    const auto path = temp_path("spectra_test_text_annotations.spectra");
    ASSERT_TRUE(Workspace::save(path, data));
    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path, loaded));
    std::remove(path.c_str());

    ASSERT_EQ(loaded.figures.size(), 1u);
    EXPECT_TRUE(loaded.figures[0].has_text_annotations);
    ASSERT_EQ(loaded.figures[0].text_annotations.size(), 3u);
    EXPECT_EQ(loaded.figures[0].text_annotations[2].axes_index, 1u);
    // Series/axes keys are still read correctly next to the new array.
    ASSERT_EQ(loaded.figures[0].axes.size(), 2u);
    EXPECT_EQ(loaded.figures[0].series.size(), 1u);

    Figure dst;
    dst.subplot(1, 2, 1).text(9.0f, 9.0f, "stale");
    dst.subplot(1, 2, 2);
    std::vector<Figure*> dst_figs = {&dst};
    ASSERT_TRUE(Workspace::apply(loaded, dst_figs));
    expect_sample_annotations(*dst.axes()[0]);
    ASSERT_EQ(dst.axes()[1]->annotations().size(), 1u);
    EXPECT_EQ(dst.axes()[1]->annotations()[0]->text(), "second axes");
}

TEST(TextAnnotationPersist, OlderWorkspaceWithoutKeyKeepsAnnotations)
{
    // A v3 workspace written before text annotations were saved.
    const auto path = temp_path("spectra_test_text_annotations_v3.spectra");
    {
        std::ofstream f(path);
        f << R"({
  "version": 3,
  "theme_name": "dark",
  "figures": [
    {
      "title": "",
      "width": 800,
      "height": 600,
      "axes": [
        {"x_min": 0, "x_max": 1, "y_min": 0, "y_max": 1, "grid_visible": true}
      ],
      "series": []
    }
  ]
}
)";
    }
    WorkspaceData loaded;
    ASSERT_TRUE(Workspace::load(path, loaded));
    std::remove(path.c_str());
    ASSERT_EQ(loaded.figures.size(), 1u);
    EXPECT_FALSE(loaded.figures[0].has_text_annotations);

    Figure fig;
    fig.subplot(1, 1, 1).text(0.5f, 0.5f, "keep me");
    std::vector<Figure*> figs = {&fig};
    ASSERT_TRUE(Workspace::apply(loaded, figs));
    ASSERT_EQ(fig.axes()[0]->annotations().size(), 1u);
    EXPECT_EQ(fig.axes()[0]->annotations()[0]->text(), "keep me");
}

TEST(TextAnnotationPersist, FigureFileRoundTrip)
{
    Figure src;
    add_sample_annotations(src.subplot(1, 1, 1));

    const auto path = temp_path("spectra_test_text_annotations.spcf");
    ASSERT_TRUE(FigureSerializer::save(path, src));

    Figure dst;
    dst.subplot(1, 1, 1).text(5.0f, 5.0f, "replaced on load");
    ASSERT_TRUE(FigureSerializer::load(path, dst));
    std::remove(path.c_str());

    ASSERT_EQ(dst.axes().size(), 1u);
    expect_sample_annotations(*dst.axes()[0]);
}
