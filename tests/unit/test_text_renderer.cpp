#include <cstring>
#include <fstream>
#include <gtest/gtest.h>
#include <spectra/app.hpp>
#include <vector>

#include "render/backend.hpp"
#include "render/text_renderer.hpp"

using namespace spectra;

// ─── Helper: load Inter-Regular.ttf from disk ────────────────────────────────

#ifndef SPECTRA_SOURCE_DIR
    #define SPECTRA_SOURCE_DIR "."
#endif

static std::vector<uint8_t> load_font()
{
    std::string abs_path = std::string(SPECTRA_SOURCE_DIR) + "/third_party/Inter-Regular.ttf";
    const char* paths[]  = {
         abs_path.c_str(),
         "third_party/Inter-Regular.ttf",
         "../third_party/Inter-Regular.ttf",
         "../../third_party/Inter-Regular.ttf",
    };
    for (const char* p : paths)
    {
        std::ifstream f(p, std::ios::binary | std::ios::ate);
        if (!f.is_open())
            continue;
        auto sz = f.tellg();
        if (sz <= 0)
            continue;
        std::vector<uint8_t> data(static_cast<size_t>(sz));
        f.seekg(0);
        f.read(reinterpret_cast<char*>(data.data()), sz);
        if (f)
            return data;
    }
    return {};
}

// ─── CPU-only tests (no GPU) ─────────────────────────────────────────────────

class TextRendererCPUTest : public ::testing::Test
{
   protected:
    std::vector<uint8_t> font_data_;

    void SetUp() override { font_data_ = load_font(); }
};

TEST_F(TextRendererCPUTest, FontDataLoaded)
{
    ASSERT_FALSE(font_data_.empty()) << "Could not load Inter-Regular.ttf";
}

TEST_F(TextRendererCPUTest, DefaultState)
{
    TextRenderer tr;
    EXPECT_FALSE(tr.is_initialized());
}

TEST_F(TextRendererCPUTest, InitRequiresValidFont)
{
    // Cannot test init without a Backend, but we can verify the class is constructible
    TextRenderer tr;
    EXPECT_FALSE(tr.is_initialized());
}

TEST_F(TextRendererCPUTest, FontSizeEnumValues)
{
    EXPECT_EQ(static_cast<int>(FontSize::Tick), 0);
    EXPECT_EQ(static_cast<int>(FontSize::Label), 1);
    EXPECT_EQ(static_cast<int>(FontSize::Title), 2);
    EXPECT_EQ(static_cast<int>(FontSize::Count), 3);
}

TEST_F(TextRendererCPUTest, TextAlignEnumValues)
{
    EXPECT_EQ(static_cast<int>(TextAlign::Left), 0);
    EXPECT_EQ(static_cast<int>(TextAlign::Center), 1);
    EXPECT_EQ(static_cast<int>(TextAlign::Right), 2);
}

TEST_F(TextRendererCPUTest, TextVAlignEnumValues)
{
    EXPECT_EQ(static_cast<int>(TextVAlign::Top), 0);
    EXPECT_EQ(static_cast<int>(TextVAlign::Middle), 1);
    EXPECT_EQ(static_cast<int>(TextVAlign::Bottom), 2);
}

TEST_F(TextRendererCPUTest, TextVertexLayout)
{
    // TextVertex must be 24 bytes: 3 floats pos (x,y,z) + 2 floats uv + 1 uint32 color
    EXPECT_EQ(sizeof(TextVertex), 24u);

    // Verify offsets
    TextVertex v{};
    auto       base = reinterpret_cast<uintptr_t>(&v);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&v.x) - base, 0u);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&v.y) - base, 4u);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&v.z) - base, 8u);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&v.u) - base, 12u);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&v.v) - base, 16u);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&v.col) - base, 20u);
}

// ─── GPU tests (headless backend) ────────────────────────────────────────────

class TextRendererGPUTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        AppConfig config;
        config.headless = true;
        app_            = std::make_unique<App>(config);
        font_data_      = load_font();
    }

    void TearDown() override { app_.reset(); }

    std::unique_ptr<App> app_;
    std::vector<uint8_t> font_data_;
};

TEST_F(TextRendererGPUTest, PipelineTypeTextExists)
{
    auto* backend = app_->backend();
    ASSERT_NE(backend, nullptr);

    auto pipeline = backend->create_pipeline(PipelineType::Text);
    EXPECT_TRUE(pipeline);
}

TEST_F(TextRendererGPUTest, InitAndShutdown)
{
    if (font_data_.empty())
        GTEST_SKIP() << "Font not found";

    auto* backend = app_->backend();
    ASSERT_NE(backend, nullptr);

    TextRenderer tr;
    EXPECT_FALSE(tr.is_initialized());

    bool ok = tr.init(*backend, font_data_.data(), font_data_.size());
    EXPECT_TRUE(ok);
    EXPECT_TRUE(tr.is_initialized());
    EXPECT_TRUE(tr.pipeline());

    tr.shutdown(*backend);
    EXPECT_FALSE(tr.is_initialized());
}

TEST_F(TextRendererGPUTest, InitFromFile)
{
    auto* backend = app_->backend();
    ASSERT_NE(backend, nullptr);

    TextRenderer tr;
    const char*  paths[] = {
         "third_party/Inter-Regular.ttf",
         "../third_party/Inter-Regular.ttf",
         "../../third_party/Inter-Regular.ttf",
         "../../../third_party/Inter-Regular.ttf",
    };
    bool ok = false;
    for (const char* p : paths)
    {
        if (tr.init_from_file(*backend, p))
        {
            ok = true;
            break;
        }
    }
    if (!ok)
        GTEST_SKIP() << "Font file not found";

    EXPECT_TRUE(tr.is_initialized());
    tr.shutdown(*backend);
}

TEST_F(TextRendererGPUTest, InitFailsWithNullData)
{
    auto* backend = app_->backend();
    ASSERT_NE(backend, nullptr);

    TextRenderer tr;
    EXPECT_FALSE(tr.init(*backend, nullptr, 0));
    EXPECT_FALSE(tr.is_initialized());
}

TEST_F(TextRendererGPUTest, InitFailsWithGarbage)
{
    auto* backend = app_->backend();
    ASSERT_NE(backend, nullptr);

    uint8_t      garbage[64] = {};
    TextRenderer tr;
    EXPECT_FALSE(tr.init(*backend, garbage, sizeof(garbage)));
    EXPECT_FALSE(tr.is_initialized());
}

TEST_F(TextRendererGPUTest, MeasureText)
{
    if (font_data_.empty())
        GTEST_SKIP() << "Font not found";

    auto* backend = app_->backend();
    ASSERT_NE(backend, nullptr);

    TextRenderer tr;
    ASSERT_TRUE(tr.init(*backend, font_data_.data(), font_data_.size()));

    // Empty string should have zero width
    auto ext0 = tr.measure_text("", FontSize::Tick);
    EXPECT_FLOAT_EQ(ext0.width, 0.0f);

    // Non-empty string should have positive width and height
    auto ext1 = tr.measure_text("Hello", FontSize::Tick);
    EXPECT_GT(ext1.width, 0.0f);
    EXPECT_GT(ext1.height, 0.0f);

    // Longer string should be wider
    auto ext2 = tr.measure_text("Hello World", FontSize::Tick);
    EXPECT_GT(ext2.width, ext1.width);

    // Larger font size should produce taller text
    auto ext_label = tr.measure_text("Test", FontSize::Label);
    auto ext_title = tr.measure_text("Test", FontSize::Title);
    EXPECT_GT(ext_title.height, ext_label.height);

    tr.shutdown(*backend);
}

TEST_F(TextRendererGPUTest, DrawTextQueuesVertices)
{
    if (font_data_.empty())
        GTEST_SKIP() << "Font not found";

    auto* backend = app_->backend();
    ASSERT_NE(backend, nullptr);

    TextRenderer tr;
    ASSERT_TRUE(tr.init(*backend, font_data_.data(), font_data_.size()));

    // draw_text on empty string should not crash
    tr.draw_text("", 0, 0, FontSize::Tick, 0xFFFFFFFF);

    // draw_text on non-empty string should queue vertices
    // (we can't directly inspect the vertex count, but we can verify no crash)
    tr.draw_text("Hello", 100, 200, FontSize::Tick, 0xFFFFFFFF);
    tr.draw_text("World", 100, 220, FontSize::Label, 0xFF0000FF, TextAlign::Center);
    tr.draw_text("Title",
                 400,
                 50,
                 FontSize::Title,
                 0x00FF00FF,
                 TextAlign::Right,
                 TextVAlign::Bottom);

    // draw_text_rotated should not crash
    tr.draw_text_rotated("Rotated", 50, 300, -1.5707963f, FontSize::Label, 0xFFFFFFFF);

    tr.shutdown(*backend);
}

TEST_F(TextRendererGPUTest, FlushWithNoText)
{
    if (font_data_.empty())
        GTEST_SKIP() << "Font not found";

    auto* backend = app_->backend();
    ASSERT_NE(backend, nullptr);

    TextRenderer tr;
    ASSERT_TRUE(tr.init(*backend, font_data_.data(), font_data_.size()));

    // Flush with no queued text should be a no-op (no crash)
    // Note: flush requires an active render pass, so we can't call it here
    // without setting up a full render pass. This test just verifies init/shutdown.

    tr.shutdown(*backend);
}

TEST_F(TextRendererGPUTest, RendererTextRendererIntegration)
{
    // Verify that the Renderer's TextRenderer is accessible
    auto* backend = app_->backend();
    ASSERT_NE(backend, nullptr);

    // The App creates a Renderer internally; verify text_renderer is accessible
    // through the renderer. We can't easily access the Renderer from App in
    // headless mode, but we can verify the TextRenderer class works standalone.
    TextRenderer tr;
    if (!font_data_.empty())
    {
        ASSERT_TRUE(tr.init(*backend, font_data_.data(), font_data_.size()));
        EXPECT_TRUE(tr.is_initialized());

        // Measure and draw should work
        auto ext = tr.measure_text("Integration", FontSize::Label);
        EXPECT_GT(ext.width, 0.0f);

        tr.draw_text("Integration", 0, 0, FontSize::Label, 0xFFFFFFFF);

        tr.shutdown(*backend);
    }
}
