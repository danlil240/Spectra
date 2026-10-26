// Regression tests for Vulkan backend fixes that have GPU-independent logic.
// The helpers under test live in src/render/vulkan/vk_frame_utils.hpp.

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <vector>

#include "render/vulkan/vk_frame_utils.hpp"

using spectra::vk::classify_ubo_slot_reuse;
using spectra::vk::clamp_line_width;
using spectra::vk::copy_readback_rows;
using spectra::vk::deferred_free_is_safe;
using spectra::vk::UboSlotReuse;

// ─── Deferred frees (bugs #14, #15, #60) ─────────────────────────────────────

TEST(BugfixVulkanDeferredFree, NotSafeUntilLaterSubmissionCompletes)
{
    // Destroyed while submission 10 was the last one made: the command buffer
    // being recorded (submission 11) may still reference it.
    EXPECT_FALSE(deferred_free_is_safe(10, 0));
    EXPECT_FALSE(deferred_free_is_safe(10, 9));
    EXPECT_FALSE(deferred_free_is_safe(10, 10));
    EXPECT_TRUE(deferred_free_is_safe(10, 11));
    EXPECT_TRUE(deferred_free_is_safe(10, 500));
}

TEST(BugfixVulkanDeferredFree, OtherWindowSubmitsDoNotFreeEarly)
{
    // Window A submits #1, window B destroys a buffer (stamp 1), then B, A, B
    // submit #2..#4 without any fence of a submission > 1 being observed.
    // The old frame-count rule would free after #4; completion tracking only
    // frees once a fence for a later submission has signalled.
    const uint64_t stamp     = 1;
    uint64_t       completed = 0;   // no fence waited yet
    EXPECT_FALSE(deferred_free_is_safe(stamp, completed));
    completed = 1;   // A's fence for submission #1 observed
    EXPECT_FALSE(deferred_free_is_safe(stamp, completed));
    completed = 2;   // a later submission's fence observed
    EXPECT_TRUE(deferred_free_is_safe(stamp, completed));
}

// ─── Line width (bug #104) ───────────────────────────────────────────────────

TEST(BugfixVulkanLineWidth, ClampsToOneWithoutWideLines)
{
    EXPECT_FLOAT_EQ(clamp_line_width(2.0f, false, 1.0f, 8.0f), 1.0f);
    EXPECT_FLOAT_EQ(clamp_line_width(0.5f, false, 1.0f, 8.0f), 1.0f);
}

TEST(BugfixVulkanLineWidth, ClampsToDeviceRangeWithWideLines)
{
    EXPECT_FLOAT_EQ(clamp_line_width(2.0f, true, 1.0f, 8.0f), 2.0f);
    EXPECT_FLOAT_EQ(clamp_line_width(20.0f, true, 1.0f, 8.0f), 8.0f);
    EXPECT_FLOAT_EQ(clamp_line_width(0.25f, true, 0.5f, 8.0f), 0.5f);
    EXPECT_FLOAT_EQ(clamp_line_width(std::numeric_limits<float>::quiet_NaN(), true, 1.0f, 8.0f),
                    1.0f);
}

// ─── Readback stride / swizzle (bugs #62, #63) ───────────────────────────────

namespace
{
std::vector<uint8_t> make_tight(uint32_t w, uint32_t h)
{
    std::vector<uint8_t> v(static_cast<size_t>(w) * h * 4);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x)
        {
            uint8_t* p = &v[(static_cast<size_t>(y) * w + x) * 4];
            p[0]       = static_cast<uint8_t>(10 + x);   // "B" or "R"
            p[1]       = static_cast<uint8_t>(100 + y);
            p[2]       = static_cast<uint8_t>(200 + x);
            p[3]       = 255;
        }
    return v;
}
}   // namespace

TEST(BugfixVulkanReadback, SameSizeNoSwizzleIsIdentity)
{
    auto src = make_tight(3, 2);
    auto dst = std::vector<uint8_t>(src.size(), 0xAB);
    copy_readback_rows(dst.data(), 3, 2, src.data(), 3, 2, false);
    EXPECT_EQ(dst, src);
}

TEST(BugfixVulkanReadback, RgbaSwapchainIsNotSwizzled)
{
    // Bug #62: an RGBA swapchain must not get R/B swapped.  The backend now
    // passes swap_red_blue=false for RGBA formats.
    auto buf      = make_tight(2, 2);
    auto original = buf;
    copy_readback_rows(buf.data(), 2, 2, buf.data(), 2, 2, false);
    EXPECT_EQ(buf, original);
}

TEST(BugfixVulkanReadback, InPlaceSwizzleSwapsRedBlue)
{
    auto buf = make_tight(2, 1);
    copy_readback_rows(buf.data(), 2, 1, buf.data(), 2, 1, true);
    EXPECT_EQ(buf[0], 200);
    EXPECT_EQ(buf[2], 10);
    EXPECT_EQ(buf[4], 201);
    EXPECT_EQ(buf[6], 11);
    EXPECT_EQ(buf[1], 100);
    EXPECT_EQ(buf[3], 255);
}

TEST(BugfixVulkanReadback, ClampedCopyKeepsCallerStrideAndZeroFills)
{
    // Bug #63: caller asked for 4x3 but the surface was only 2x2.
    const uint32_t       dst_w = 4, dst_h = 3, src_w = 2, src_h = 2;
    auto                 src = make_tight(src_w, src_h);
    std::vector<uint8_t> dst(static_cast<size_t>(dst_w) * dst_h * 4, 0xCD);

    copy_readback_rows(dst.data(), dst_w, dst_h, src.data(), src_w, src_h, false);

    for (uint32_t y = 0; y < dst_h; ++y)
    {
        for (uint32_t x = 0; x < dst_w; ++x)
        {
            const uint8_t* p = &dst[(static_cast<size_t>(y) * dst_w + x) * 4];
            if (x < src_w && y < src_h)
            {
                const uint8_t* q = &src[(static_cast<size_t>(y) * src_w + x) * 4];
                EXPECT_EQ(p[0], q[0]) << x << "," << y;
                EXPECT_EQ(p[1], q[1]) << x << "," << y;
                EXPECT_EQ(p[2], q[2]) << x << "," << y;
                EXPECT_EQ(p[3], q[3]) << x << "," << y;
            }
            else
            {
                EXPECT_EQ(p[0], 0) << x << "," << y;
                EXPECT_EQ(p[1], 0) << x << "," << y;
                EXPECT_EQ(p[2], 0) << x << "," << y;
                EXPECT_EQ(p[3], 0) << x << "," << y;
            }
        }
    }
}

TEST(BugfixVulkanReadback, ClampedCopySwizzlesOnlyCopiedPixels)
{
    auto                 src = make_tight(1, 1);
    std::vector<uint8_t> dst(2 * 1 * 4, 0xEE);
    copy_readback_rows(dst.data(), 2, 1, src.data(), 1, 1, true);
    EXPECT_EQ(dst[0], 200);
    EXPECT_EQ(dst[2], 10);
    for (int i = 4; i < 8; ++i)
        EXPECT_EQ(dst[i], 0);
}

// ─── Dynamic UBO ring (bug #61) ──────────────────────────────────────────────

TEST(BugfixVulkanUboRing, NeverUsedOrCompletedSlotIsFree)
{
    EXPECT_EQ(classify_ubo_slot_reuse(0, 5, 0), UboSlotReuse::Free);
    EXPECT_EQ(classify_ubo_slot_reuse(3, 5, 3), UboSlotReuse::Free);
    EXPECT_EQ(classify_ubo_slot_reuse(3, 5, 4), UboSlotReuse::Free);
}

TEST(BugfixVulkanUboRing, InFlightSlotRequiresWait)
{
    // Slot written for submission 4, which has not completed (completed=3),
    // and we are now recording submission 5.
    EXPECT_EQ(classify_ubo_slot_reuse(4, 5, 3), UboSlotReuse::WaitForGpu);
}

TEST(BugfixVulkanUboRing, SameFrameReuseIsOverflow)
{
    EXPECT_EQ(classify_ubo_slot_reuse(5, 5, 3), UboSlotReuse::FrameOverflow);
}
