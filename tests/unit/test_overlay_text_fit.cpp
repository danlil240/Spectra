// Window-width fitting of hover overlays (audit S13): data tooltips (2D/3D)
// and crosshair readouts must stay inside small windows / viewports for long
// unbroken identifiers, long words and UTF-8 labels, without hiding values.

#include <algorithm>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "ui/overlay/overlay_text_fit.hpp"

#if defined(SPECTRA_USE_IMGUI) && __has_include(<imgui.h>)
    #define SPECTRA_TEST_OVERLAY_DRAW 1
    #include <imgui.h>
    #include <spectra/axes.hpp>
    #include <spectra/axes3d.hpp>
    #include <spectra/figure.hpp>
    #include <spectra/series.hpp>

    #include "ui/input/input.hpp"
    #include "ui/overlay/crosshair.hpp"
    #include "ui/overlay/tooltip.hpp"
    #include "ui/theme/theme.hpp"
#endif

using namespace spectra;
namespace fit = spectra::ui::overlay_fit;

namespace
{

// Fixed-advance measure: 7 px per codepoint (continuation bytes are free).
float measure7(std::string_view s)
{
    float w = 0.0f;
    for (unsigned char c : s)
        if ((c & 0xC0u) != 0x80u)
            w += 7.0f;
    return w;
}

bool is_valid_utf8(std::string_view s)
{
    size_t i = 0;
    while (i < s.size())
    {
        const auto c   = static_cast<unsigned char>(s[i]);
        size_t     len = c < 0x80           ? 1
                         : (c >> 5) == 0x6  ? 2
                         : (c >> 4) == 0xE  ? 3
                         : (c >> 3) == 0x1E ? 4
                                            : 0;
        if (len == 0 || i + len > s.size())
            return false;
        for (size_t k = 1; k < len; ++k)
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0u) != 0x80u)
                return false;
        i += len;
    }
    return true;
}

const std::string LONG_ID(120, 'x');   // long unbroken identifier
const std::string LONG_WORDS =
    "a rather descriptive series label with many words that keeps going and going";
const std::string ROS_PATH = "/robot/arm/joint_states/position/shoulder_pan_joint/filtered";
// "Température capteur" + CJK "sensor temperature" repeated: multi-byte codepoints.
const std::string UTF8_LABEL =
    "Temp\xC3\xA9rature capteur \xE4\xBC\xA0\xE6\x84\x9F\xE5\x99\xA8\xE6\xB8\xA9\xE5\xBA\xA6"
    "\xE4\xBC\xA0\xE6\x84\x9F\xE5\x99\xA8\xE6\xB8\xA9\xE5\xBA\xA6\xE4\xBC\xA0\xE6\x84\x9F\xE5"
    "\x99\xA8\xE6\xB8\xA9\xE5\xBA\xA6\xE4\xBC\xA0\xE6\x84\x9F\xE5\x99\xA8\xE6\xB8\xA9\xE5\xBA"
    "\xA6\xE4\xBC\xA0\xE6\x84\x9F\xE5\x99\xA8\xE6\xB8\xA9\xE5\xBA\xA6";

}   // namespace

// ─── Pure helpers ───────────────────────────────────────────────────────────

TEST(OverlayTextFit, EllipsizeKeepsFittingTextAndShortensLongText)
{
    EXPECT_EQ(fit::ellipsize("short", 100.0f, measure7), "short");

    const std::string e = fit::ellipsize(LONG_ID, 140.0f, measure7);
    EXPECT_LE(measure7(e), 140.0f);
    EXPECT_EQ(e.substr(e.size() - 3), "...");
    EXPECT_EQ(e, std::string(17, 'x') + "...");   // 20 codepoints * 7 px

    EXPECT_EQ(fit::ellipsize(LONG_ID, 10.0f, measure7), "");   // not even "..." fits
}

TEST(OverlayTextFit, EllipsizeNeverSplitsUtf8)
{
    for (float w = 21.0f; w < 300.0f; w += 7.0f)
    {
        const std::string e = fit::ellipsize(UTF8_LABEL, w, measure7);
        EXPECT_LE(measure7(e), w);
        EXPECT_TRUE(is_valid_utf8(e)) << "width " << w;
    }
}

TEST(OverlayTextFit, WrapUnbrokenIdentifierKeepsAllText)
{
    const auto lines = fit::wrap_lines(std::string(50, 'q'), 140.0f, 3, measure7);
    ASSERT_EQ(lines.size(), 3u);
    std::string joined;
    for (const auto& l : lines)
    {
        EXPECT_LE(measure7(l), 140.0f);
        joined += l;
    }
    EXPECT_EQ(joined, std::string(50, 'q'));
}

TEST(OverlayTextFit, WrapPrefersSeparators)
{
    const auto lines = fit::wrap_lines(ROS_PATH, 210.0f, 3, measure7);
    ASSERT_GE(lines.size(), 2u);
    for (const auto& l : lines)
        EXPECT_LE(measure7(l), 210.0f);
    // First line ends right after a path/field separator, not mid-token.
    const char last = lines[0].back();
    EXPECT_TRUE(last == '/' || last == '_') << lines[0];

    const auto words = fit::wrap_lines(LONG_WORDS, 140.0f, 2, measure7);
    ASSERT_EQ(words.size(), 2u);
    EXPECT_EQ(words[0], "a rather descriptive");   // broke at a space, space trimmed
    EXPECT_EQ(words[1].substr(words[1].size() - 3), "...");
    EXPECT_LE(measure7(words[1]), 140.0f);
}

TEST(OverlayTextFit, WrapUtf8StaysValid)
{
    const auto lines = fit::wrap_lines(UTF8_LABEL, 70.0f, 3, measure7);
    ASSERT_EQ(lines.size(), 3u);
    for (const auto& l : lines)
    {
        EXPECT_TRUE(is_valid_utf8(l));
        EXPECT_LE(measure7(l), 70.0f);
    }
}

TEST(OverlayTextFit, TooltipContentNeverHidesValues)
{
    const std::string_view labels[] = {"X", "Y", LONG_ID};
    const std::string_view values[] = {"1700000000.123456", "-0.000123", "42"};

    const auto c = fit::fit_tooltip_content(LONG_ID,
                                            16.0f,
                                            labels,
                                            values,
                                            10.0f,
                                            140.0f,
                                            300.0f,
                                            fit::MAX_NAME_LINES,
                                            measure7);
    EXPECT_FLOAT_EQ(c.value_w, measure7("1700000000.123456"));
    EXPECT_LE(c.content_w, 300.0f);
    EXPECT_LE(c.label_w + 10.0f + c.value_w, c.content_w);
    EXPECT_EQ(c.labels[0], "X");
    EXPECT_EQ(c.labels[2].substr(c.labels[2].size() - 3), "...");
    EXPECT_LE(c.name_w, c.content_w);
    EXPECT_EQ(c.name_lines.size(), static_cast<size_t>(fit::MAX_NAME_LINES));

    // Window narrower than the numbers themselves: values still win.
    const auto tiny = fit::fit_tooltip_content(LONG_ID,
                                               16.0f,
                                               labels,
                                               values,
                                               10.0f,
                                               140.0f,
                                               60.0f,
                                               fit::MAX_NAME_LINES,
                                               measure7);
    EXPECT_GE(tiny.content_w, tiny.value_w + 10.0f);
}

TEST(OverlayTextFit, PlaceNearAnchorClampsEveryEdgeAndCorner)
{
    const float W = 400.0f, H = 300.0f;
    const float anchors[][2] =
        {{0, 0}, {W, 0}, {0, H}, {W, H}, {W / 2, H / 2}, {-50, -50}, {W + 50, H + 50}};
    for (float bw : {100.0f, 392.0f, 500.0f})
    {
        for (const auto& a : anchors)
        {
            const fit::Box b =
                fit::place_near_anchor(a[0], a[1], bw, 80.0f, 14.0f, 8.0f, 16.0f, W, H);
            EXPECT_GE(b.x, fit::WINDOW_MARGIN);
            EXPECT_GE(b.y, fit::WINDOW_MARGIN);
            if (bw <= W - 2 * fit::WINDOW_MARGIN)
            {
                EXPECT_LE(b.x + b.w, W - fit::WINDOW_MARGIN);
            }
            EXPECT_LE(b.y + b.h, H - fit::WINDOW_MARGIN);
        }
    }
    // Normal placement is unchanged: above-right of the anchor.
    const fit::Box b = fit::place_near_anchor(100, 200, 100, 50, 14, 8, 16, W, H);
    EXPECT_FLOAT_EQ(b.x, 114.0f);
    EXPECT_FLOAT_EQ(b.y, 142.0f);
}

// ─── Real ImGui draws ───────────────────────────────────────────────────────

#ifdef SPECTRA_TEST_OVERLAY_DRAW
namespace
{

class ImGuiOverlayTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        ImGui::CreateContext();
        ImGuiIO& io    = ImGui::GetIO();
        io.DisplaySize = ImVec2(600.0f, 400.0f);
        io.DeltaTime   = 1.0f;   // fade straight to full opacity
        io.Fonts->AddFontDefault();
        unsigned char* px = nullptr;
        int            w = 0, h = 0;
        io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
        ImGui::NewFrame();
        theme_.ensure_initialized();
    }
    void TearDown() override
    {
        ImGui::EndFrame();
        ImGui::DestroyContext();
    }

    // Bounding box of all vertices emitted into dl.
    static ImVec4 bounds(const ImDrawList& dl)
    {
        ImVec4 b(1e9f, 1e9f, -1e9f, -1e9f);
        for (const ImDrawVert& v : dl.VtxBuffer)
        {
            b.x = std::min(b.x, v.pos.x);
            b.y = std::min(b.y, v.pos.y);
            b.z = std::max(b.z, v.pos.x);
            b.w = std::max(b.w, v.pos.y);
        }
        return b;
    }

    static std::unique_ptr<ImDrawList> new_list()
    {
        auto dl = std::make_unique<ImDrawList>(ImGui::GetDrawListSharedData());
        dl->_ResetForNewFrame();
        dl->PushClipRectFullScreen();
        dl->PushTexture(ImGui::GetIO().Fonts->TexRef);
        return dl;
    }

    ui::ThemeManager theme_;
};

}   // namespace

TEST_F(ImGuiOverlayTest, TooltipStaysInsideSmallWindowsAtEveryCorner)
{
    const std::string names[] = {LONG_ID, LONG_WORDS, ROS_PATH, UTF8_LABEL};
    for (bool is_3d : {false, true})
    {
        for (float W : {400.0f, 500.0f, 600.0f})
        {
            const float H = 300.0f;
            // Anchors near each corner, each edge midpoint and the centre.
            const float anchors[][2] = {{20, 20},
                                        {W - 20, 20},
                                        {20, H - 20},
                                        {W - 20, H - 20},
                                        {W / 2, 20},
                                        {W / 2, H - 20},
                                        {20, H / 2},
                                        {W - 20, H / 2},
                                        {W / 2, H / 2}};
            for (const auto& name : names)
            {
                Figure             fig({.width = static_cast<uint32_t>(W), .height = 300});
                auto&              ax     = fig.subplot(1, 1, 1);
                std::vector<float> xs     = {0.0f, 1.0f};
                std::vector<float> ys     = {1.7e9f, 2.0f};
                auto&              series = ax.line(xs, ys);
                series.label(name);

                for (const auto& a : anchors)
                {
                    Tooltip tip;
                    tip.set_theme_manager(&theme_);
                    tip.set_fonts(ImGui::GetIO().Fonts->Fonts[0], nullptr);

                    NearestPointResult np;
                    np.found       = true;
                    np.series      = &series;
                    np.data_x      = 1700000000.123456;
                    np.data_y      = -12345.678901;
                    np.data_z      = 3.14159f;
                    np.dy_dx       = 1.0f;
                    np.dy_dx_valid = true;
                    np.screen_x    = a[0];
                    np.screen_y    = a[1];
                    np.distance_px = 0.0f;
                    np.is_3d       = is_3d;

                    auto dl = new_list();
                    tip.draw(np, W, H, dl.get());
                    ASSERT_GT(dl->VtxBuffer.Size, 0);
                    const ImVec4 b = bounds(*dl);
                    // The drop shadow (+4 px) reaches the window edge and
                    // anti-aliasing adds a fringe of up to 1 px.
                    constexpr float AA = 1.0f;
                    EXPECT_GE(b.x, 0.0f) << (is_3d ? "3D " : "2D ") << W << " " << name;
                    EXPECT_GE(b.y, 0.0f) << (is_3d ? "3D " : "2D ") << W << " " << name;
                    EXPECT_LE(b.z, W + AA) << (is_3d ? "3D " : "2D ") << W << " " << name << " @"
                                           << a[0] << "," << a[1];
                    EXPECT_LE(b.w, H + AA) << (is_3d ? "3D " : "2D ") << W << " " << name;
                }
            }
        }
    }
}

TEST_F(ImGuiOverlayTest, CrosshairLabelsStayInsideNarrowViewport)
{
    Crosshair ch;
    ch.set_theme_manager(&theme_);
    ch.set_enabled(true);

    // A 60 px wide subplot with epoch-scale X and large Y readouts.
    const Rect    vp{100.0f, 50.0f, 60.0f, 200.0f};
    CursorReadout cur;
    cur.valid = true;
    for (float sx : {115.0f, 130.0f, 145.0f})
    {
        for (float sy : {60.0f, 150.0f, 240.0f})
        {
            cur.screen_x = sx;
            cur.screen_y = sy;
            cur.data_x   = 1700000000.123456;
            cur.data_y   = -123456789.98765;
            auto dl      = new_list();
            ch.draw(cur, vp, 1.6e9f, 1.8e9f, -2e8f, 0.0f, dl.get());
            ASSERT_GT(dl->VtxBuffer.Size, 0);
            const ImVec4 b = bounds(*dl);
            EXPECT_GE(b.x, vp.x - 1.0f) << sx << "," << sy;
            EXPECT_LE(b.z, vp.x + vp.w + 1.0f) << sx << "," << sy;
        }
    }
}
#endif
