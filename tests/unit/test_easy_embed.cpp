#include <gtest/gtest.h>
#include <spectra/easy_embed.hpp>

#include <cmath>
#include <filesystem>
#include <vector>

// ─── Basic Rendering ────────────────────────────────────────────────────────

TEST(EasyEmbed, RenderLineBasic)
{
    std::vector<float> x = {0, 1, 2, 3, 4};
    std::vector<float> y = {0, 1, 4, 9, 16};
    auto img = spectra::render(x, y);
    EXPECT_FALSE(img.empty());
    EXPECT_EQ(img.width, 800u);
    EXPECT_EQ(img.height, 600u);
    EXPECT_EQ(img.size_bytes(), 800u * 600u * 4u);
    EXPECT_EQ(img.stride(), 800u * 4u);
}

TEST(EasyEmbed, RenderLineCustomSize)
{
    std::vector<float> x = {0, 1, 2, 3};
    std::vector<float> y = {0, 1, 4, 9};
    spectra::RenderOptions opts;
    opts.width  = 400;
    opts.height = 300;
    auto img = spectra::render(x, y, opts);
    EXPECT_FALSE(img.empty());
    EXPECT_EQ(img.width, 400u);
    EXPECT_EQ(img.height, 300u);
}

TEST(EasyEmbed, RenderLineNonBlank)
{
    std::vector<float> x = {0, 1, 2, 3, 4, 5};
    std::vector<float> y = {0, 1, 4, 9, 16, 25};
    auto img = spectra::render(x, y);

    // Check that pixels are not all zero (something was rendered)
    int nonzero = 0;
    for (size_t i = 0; i < img.size_bytes(); ++i)
        if (img.pixels()[i] != 0)
            nonzero++;
    EXPECT_GT(nonzero, 100);
}

// ─── Format String ──────────────────────────────────────────────────────────

TEST(EasyEmbed, RenderWithFormatString)
{
    std::vector<float> x = {0, 1, 2, 3};
    std::vector<float> y = {0, 1, 4, 9};
    spectra::RenderOptions opts;
    opts.fmt = "r--o";
    auto img = spectra::render(x, y, opts);
    EXPECT_FALSE(img.empty());
    EXPECT_EQ(img.width, 800u);
}

// ─── Scatter ────────────────────────────────────────────────────────────────

TEST(EasyEmbed, RenderScatter)
{
    std::vector<float> x = {0, 1, 2, 3, 4};
    std::vector<float> y = {2, 3, 1, 5, 4};
    auto img = spectra::render_scatter(x, y);
    EXPECT_FALSE(img.empty());
    EXPECT_EQ(img.width, 800u);
    EXPECT_EQ(img.height, 600u);
}

// ─── Multi-Series ───────────────────────────────────────────────────────────

TEST(EasyEmbed, RenderMultiSeries)
{
    std::vector<float> x = {0, 1, 2, 3, 4};
    std::vector<float> y1 = {0, 1, 4, 9, 16};
    std::vector<float> y2 = {0, 1, 2, 3, 4};

    auto img = spectra::render_multi(
        {
            {x, y1, "-", "quadratic"},
            {x, y2, "-", "linear"},
        });
    EXPECT_FALSE(img.empty());
}

TEST(EasyEmbed, RenderMultiSeriesVector)
{
    std::vector<float> x = {0, 1, 2, 3};
    std::vector<float> y1 = {0, 1, 4, 9};
    std::vector<float> y2 = {9, 4, 1, 0};

    std::vector<spectra::SeriesDesc> series = {
        {x, y1, "-", "ascending"},
        {x, y2, "-", "descending"},
    };
    auto img = spectra::render_multi(series);
    EXPECT_FALSE(img.empty());
}

// ─── Options ────────────────────────────────────────────────────────────────

TEST(EasyEmbed, RenderWithTitle)
{
    std::vector<float> x = {0, 1, 2, 3};
    std::vector<float> y = {0, 1, 4, 9};
    spectra::RenderOptions opts;
    opts.title  = "Test Plot";
    opts.xlabel = "X Axis";
    opts.ylabel = "Y Axis";
    auto img = spectra::render(x, y, opts);
    EXPECT_FALSE(img.empty());
}

TEST(EasyEmbed, RenderWithGrid)
{
    std::vector<float> x = {0, 1, 2, 3};
    std::vector<float> y = {0, 1, 4, 9};
    spectra::RenderOptions opts;
    opts.grid = false;
    auto img = spectra::render(x, y, opts);
    EXPECT_FALSE(img.empty());
}

// ─── Save to PNG ────────────────────────────────────────────────────────────

TEST(EasyEmbed, SavePng)
{
    std::vector<float> x = {0, 1, 2, 3, 4};
    std::vector<float> y = {0, 1, 4, 9, 16};

    std::string path = "test_easy_embed_output.png";
    spectra::RenderOptions opts;
    opts.save_path = path;
    auto img = spectra::render(x, y, opts);
    EXPECT_FALSE(img.empty());

    // Check file was created
    EXPECT_TRUE(std::filesystem::exists(path));
    EXPECT_GT(std::filesystem::file_size(path), 0u);

    // Cleanup
    std::filesystem::remove(path);
}

TEST(EasyEmbed, SavePngWithOptions)
{
    std::vector<float> x = {0, 1, 2, 3};
    std::vector<float> y = {0, 1, 4, 9};

    std::string path = "test_easy_embed_opts.png";
    spectra::RenderOptions opts;
    opts.width     = 400;
    opts.height    = 300;
    opts.save_path = path;
    opts.title     = "Saved Plot";
    auto img = spectra::render(x, y, opts);
    EXPECT_FALSE(img.empty());
    EXPECT_TRUE(std::filesystem::exists(path));
    std::filesystem::remove(path);
}

TEST(EasyEmbed, SaveScatterPng)
{
    std::vector<float> x = {0, 1, 2, 3};
    std::vector<float> y = {3, 1, 4, 2};

    std::string path = "test_easy_scatter.png";
    spectra::RenderOptions opts;
    opts.save_path = path;
    auto img = spectra::render_scatter(x, y, opts);
    EXPECT_FALSE(img.empty());
    EXPECT_TRUE(std::filesystem::exists(path));
    std::filesystem::remove(path);
}

// ─── Histogram ──────────────────────────────────────────────────────────────

TEST(EasyEmbed, RenderHistogram)
{
    std::vector<float> values;
    // Generate some test data
    for (int i = 0; i < 1000; ++i)
        values.push_back(static_cast<float>(i % 100) / 10.0f);

    auto img = spectra::render_histogram(values, 20);
    EXPECT_FALSE(img.empty());
}

// ─── Bar Chart ──────────────────────────────────────────────────────────────

TEST(EasyEmbed, RenderBar)
{
    std::vector<float> positions = {1, 2, 3, 4, 5};
    std::vector<float> heights   = {10, 25, 15, 30, 20};

    auto img = spectra::render_bar(positions, heights);
    EXPECT_FALSE(img.empty());
}

// ─── Edge Cases ─────────────────────────────────────────────────────────────

TEST(EasyEmbed, RenderSmallSize)
{
    std::vector<float> x = {0, 1};
    std::vector<float> y = {0, 1};
    spectra::RenderOptions opts;
    opts.width  = 64;
    opts.height = 64;
    auto img = spectra::render(x, y, opts);
    EXPECT_FALSE(img.empty());
    EXPECT_EQ(img.width, 64u);
    EXPECT_EQ(img.height, 64u);
}

TEST(EasyEmbed, RenderLargeDataset)
{
    std::vector<float> x(10000);
    std::vector<float> y(10000);
    for (int i = 0; i < 10000; ++i)
    {
        x[i] = static_cast<float>(i) / 100.0f;
        y[i] = std::sin(x[i]);
    }
    auto img = spectra::render(x, y);
    EXPECT_FALSE(img.empty());
}

TEST(EasyEmbed, MultipleRendersSequential)
{
    std::vector<float> x = {0, 1, 2, 3};
    std::vector<float> y = {0, 1, 4, 9};

    for (int i = 0; i < 3; ++i)
    {
        auto img = spectra::render(x, y);
        EXPECT_FALSE(img.empty());
    }
}

TEST(EasyEmbed, PixelsAccessor)
{
    std::vector<float> x = {0, 1, 2};
    std::vector<float> y = {0, 1, 4};
    auto img = spectra::render(x, y);
    EXPECT_NE(img.pixels(), nullptr);

    const auto& cimg = img;
    EXPECT_NE(cimg.pixels(), nullptr);
}
