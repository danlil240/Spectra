// test_bugfix_render_draw.cpp — Regression tests for render-draw fixes that are
// testable without a GPU: polyline draw-range culling and the custom series
// type registry (owned descriptor storage, retired pipelines).

#include <cmath>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "render/renderer.hpp"
#include "render/series_type_registry.hpp"

using namespace spectra;

// ─── compute_line_draw_range ─────────────────────────────────────────────────

namespace
{

std::vector<float> iota_floats(size_t n, float start = 0.0f)
{
    std::vector<float> v(n);
    for (size_t i = 0; i < n; ++i)
        v[i] = start + static_cast<float>(i);
    return v;
}

void expect_within(const LineDrawRange& r, size_t n)
{
    EXPECT_LE(static_cast<size_t>(r.first_pt) + r.pt_count, n);
    if (r.seg_count > 0)
        EXPECT_LE(static_cast<size_t>(r.first_seg) + r.seg_count + 1, n);
}

}   // namespace

TEST(BugfixRenderDraw, NoCullDrawsEverything)
{
    auto x = iota_floats(10);
    auto r = compute_line_draw_range(x, x.size(), true, false, 0.0f, 0.0f);
    EXPECT_EQ(r.first_pt, 0u);
    EXPECT_EQ(r.pt_count, 10u);
    EXPECT_EQ(r.first_seg, 0u);
    EXPECT_EQ(r.seg_count, 9u);
}

TEST(BugfixRenderDraw, UnsortedDrawsEverything)
{
    std::vector<float> x = {3, 1, 2, 0, 5};
    auto               r = compute_line_draw_range(x, x.size(), false, true, 1.0f, 2.0f);
    EXPECT_EQ(r.pt_count, 5u);
    EXPECT_EQ(r.seg_count, 4u);
}

TEST(BugfixRenderDraw, SortedCullsToVisibleWindowWithMargin)
{
    auto x = iota_floats(1000);
    auto r = compute_line_draw_range(x, x.size(), true, true, 100.0f, 200.0f);
    // One point of margin on either side.
    EXPECT_EQ(r.first_pt, 99u);
    EXPECT_EQ(r.pt_count, 103u);   // indices 99..201
    EXPECT_EQ(r.first_seg, 99u);
    EXPECT_EQ(r.seg_count, 102u);
    expect_within(r, x.size());
}

// Bug #48: x longer than y — only point_count() points are uploaded, so the
// draw range must never exceed n even though x has more elements.
TEST(BugfixRenderDraw, RangeClampedToUploadedCountWhenXLonger)
{
    auto         x = iota_floats(3000);
    const size_t n = 500;   // min(x.size(), y.size())

    auto all = compute_line_draw_range(x, n, true, true, -1.0f, 5000.0f);
    EXPECT_EQ(all.first_pt, 0u);
    EXPECT_EQ(all.pt_count, n);
    EXPECT_EQ(all.seg_count, n - 1);
    expect_within(all, n);

    auto tail = compute_line_draw_range(x, n, true, true, 2000.0f, 2500.0f);
    expect_within(tail, n);

    auto nocull = compute_line_draw_range(x, n, true, false, 0.0f, 0.0f);
    EXPECT_EQ(nocull.pt_count, n);
    EXPECT_EQ(nocull.seg_count, n - 1);
}

TEST(BugfixRenderDraw, RangeClampedToXSizeWhenNLarger)
{
    auto x = iota_floats(10);
    auto r = compute_line_draw_range(x, 100, true, false, 0.0f, 0.0f);
    EXPECT_EQ(r.pt_count, 10u);
    EXPECT_EQ(r.seg_count, 9u);
}

// Bug #50: callers pass series-local bounds (absolute - x_offset).  With a
// large offset, the local window must select the matching local indices.
TEST(BugfixRenderDraw, LocalBoundsWithLargeOffset)
{
    auto         x       = iota_floats(1000);   // local x in [0, 999]
    const double xoff    = 1.7e9;
    const double abs_min = xoff + 0.0;
    const double abs_max = xoff + 999.0;
    auto         r       = compute_line_draw_range(x,
                                     x.size(),
                                     true,
                                     true,
                                     static_cast<float>(abs_min - xoff),
                                     static_cast<float>(abs_max - xoff));
    EXPECT_EQ(r.first_pt, 0u);
    EXPECT_EQ(r.pt_count, 1000u);
    EXPECT_EQ(r.seg_count, 999u);
}

TEST(BugfixRenderDraw, NanBoundsDisableCulling)
{
    auto        x   = iota_floats(50);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    auto        r   = compute_line_draw_range(x, x.size(), true, true, nan, 10.0f);
    EXPECT_EQ(r.pt_count, 50u);
    EXPECT_EQ(r.seg_count, 49u);
}

TEST(BugfixRenderDraw, EmptyAndTinyInputs)
{
    std::vector<float> empty;
    auto               r0 = compute_line_draw_range(empty, 0, true, true, 0.0f, 1.0f);
    EXPECT_EQ(r0.pt_count, 0u);
    EXPECT_EQ(r0.seg_count, 0u);

    std::vector<float> one = {1.0f};
    auto               r1  = compute_line_draw_range(one, 1, true, true, 0.0f, 1.0f);
    EXPECT_EQ(r1.pt_count, 1u);
    EXPECT_EQ(r1.seg_count, 0u);
}

// ─── SeriesTypeRegistry ──────────────────────────────────────────────────────

namespace
{

const uint8_t kStubSpirv[] = {
    0x03, 0x02, 0x23, 0x07, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

void register_with_stack_layout(SeriesTypeRegistry& reg, const char* name)
{
    CustomPipelineDesc::VertexBinding   bindings[1]   = {{0, 8, 0}};
    CustomPipelineDesc::VertexAttribute attributes[2] = {{0, 0, 103, 0}, {1, 0, 100, 4}};

    CustomPipelineDesc pd;
    pd.vert_spirv             = kStubSpirv;
    pd.vert_spirv_size        = sizeof(kStubSpirv);
    pd.frag_spirv             = kStubSpirv;
    pd.frag_spirv_size        = sizeof(kStubSpirv);
    pd.vertex_bindings        = bindings;
    pd.vertex_binding_count   = 1;
    pd.vertex_attributes      = attributes;
    pd.vertex_attribute_count = 2;

    // Upload/cleanup lambdas taking gpu_state by value must still be accepted
    // (source compatibility with the by-reference slot signature).
    reg.register_type(
        name,
        pd,
        [](Backend&, const void*, void*, size_t) -> int { return 0; },
        [](Backend&, PipelineHandle, const void*, const float*, const SeriesPushConstants&) -> int
        { return 0; },
        [](const void*, size_t, float*) -> int { return 0; },
        [](Backend&, void*) {});

    // Scribble over the plugin's stack arrays to emulate them going out of scope.
    bindings[0]   = {99, 99, 99};
    attributes[0] = {99, 99, 99, 99};
    attributes[1] = {99, 99, 99, 99};
}

}   // namespace

// Bug #57: the vertex layout must be deep-copied at registration.
TEST(BugfixRenderDraw, RegistryCopiesVertexLayout)
{
    SeriesTypeRegistry reg;
    register_with_stack_layout(reg, "layout_a");
    // Register more types to force the internal vector to reallocate.
    for (int i = 0; i < 16; ++i)
        register_with_stack_layout(reg, ("filler_" + std::to_string(i)).c_str());

    const auto* entry = reg.find("layout_a");
    ASSERT_NE(entry, nullptr);
    const auto& d = entry->pipeline_desc;
    ASSERT_EQ(d.vertex_binding_count, 1u);
    ASSERT_EQ(d.vertex_attribute_count, 2u);
    ASSERT_NE(d.vertex_bindings, nullptr);
    ASSERT_NE(d.vertex_attributes, nullptr);
    EXPECT_EQ(d.vertex_bindings, entry->vertex_binding_storage.data());
    EXPECT_EQ(d.vertex_attributes, entry->vertex_attribute_storage.data());
    EXPECT_EQ(d.vertex_bindings[0].stride, 8u);
    EXPECT_EQ(d.vertex_bindings[0].binding, 0u);
    EXPECT_EQ(d.vertex_attributes[0].format, 103u);
    EXPECT_EQ(d.vertex_attributes[1].location, 1u);
    EXPECT_EQ(d.vertex_attributes[1].offset, 4u);
    EXPECT_EQ(d.vert_spirv, entry->vert_spirv_storage.data());
    EXPECT_EQ(d.frag_spirv, entry->frag_spirv_storage.data());
}

TEST(BugfixRenderDraw, RegistryWithoutVertexLayoutHasNullPointers)
{
    SeriesTypeRegistry reg;
    CustomPipelineDesc pd;
    pd.vert_spirv      = kStubSpirv;
    pd.vert_spirv_size = sizeof(kStubSpirv);
    pd.frag_spirv      = kStubSpirv;
    pd.frag_spirv_size = sizeof(kStubSpirv);
    reg.register_type("ssbo_only", pd, {}, {}, {});
    const auto* entry = reg.find("ssbo_only");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->pipeline_desc.vertex_bindings, nullptr);
    EXPECT_EQ(entry->pipeline_desc.vertex_binding_count, 0u);
    EXPECT_EQ(entry->pipeline_desc.vertex_attributes, nullptr);
    EXPECT_EQ(entry->pipeline_desc.vertex_attribute_count, 0u);
}

// Bug #108: unregistering a type must hand its pipeline over for deferred
// destruction instead of dropping the handle.
TEST(BugfixRenderDraw, UnregisterRetiresPipeline)
{
    SeriesTypeRegistry reg;
    register_with_stack_layout(reg, "retire_me");
    register_with_stack_layout(reg, "keep_me");

    auto* entry = reg.find_mut("retire_me");
    ASSERT_NE(entry, nullptr);
    entry->pipeline.id                   = 42;
    reg.find_mut("keep_me")->pipeline.id = 7;

    EXPECT_TRUE(reg.take_retired_pipelines().empty());

    reg.unregister_type("retire_me");
    EXPECT_EQ(reg.find("retire_me"), nullptr);

    auto retired = reg.take_retired_pipelines();
    ASSERT_EQ(retired.size(), 1u);
    EXPECT_EQ(retired[0].id, 42u);
    EXPECT_TRUE(reg.take_retired_pipelines().empty());

    // The surviving entry keeps its pipeline and owned layout.
    const auto* kept = reg.find("keep_me");
    ASSERT_NE(kept, nullptr);
    EXPECT_EQ(kept->pipeline.id, 7u);
    EXPECT_EQ(kept->pipeline_desc.vertex_bindings, kept->vertex_binding_storage.data());
}

TEST(BugfixRenderDraw, UnregisterWithoutPipelineRetiresNothing)
{
    SeriesTypeRegistry reg;
    register_with_stack_layout(reg, "no_pipeline");
    reg.unregister_type("no_pipeline");
    EXPECT_TRUE(reg.take_retired_pipelines().empty());
}
