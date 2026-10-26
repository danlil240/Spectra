#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <spectra/series.hpp>
#include <spectra/series3d.hpp>
#include <vector>

#include "core/pending_series_data.hpp"

using namespace spectra;

namespace
{
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
}

// ── Bug #49: NaN-separated traces must not be treated as sorted ─────────────

TEST(BugfixR2Series, SortedHelperRejectsNaN)
{
    std::vector<float> a{0.0f, 1.0f, kNaN, 0.0f, 1.0f};
    EXPECT_FALSE(x_sorted_without_nan(a.begin(), a.end()));
    std::vector<float> b{kNaN, 1.0f};
    EXPECT_FALSE(x_sorted_without_nan(b.begin(), b.end()));
    std::vector<float> c{0.0f, 1.0f, 1.0f, 2.0f};
    EXPECT_TRUE(x_sorted_without_nan(c.begin(), c.end()));
    std::vector<float> d;
    EXPECT_TRUE(x_sorted_without_nan(d.begin(), d.end()));
}

TEST(BugfixR2Series, LineConstructorWithNaNIsUnsorted)
{
    std::vector<float> x{0.0f, 1.0f, kNaN, 0.0f, 1.0f};
    std::vector<float> y{0.0f, 1.0f, 2.0f, 3.0f, 4.0f};
    LineSeries         s(x, y);
    EXPECT_FALSE(s.x_is_sorted());

    std::vector<float> xs{0.0f, 1.0f, 2.0f};
    std::vector<float> ys{0.0f, 1.0f, 2.0f};
    LineSeries         sorted(xs, ys);
    EXPECT_TRUE(sorted.x_is_sorted());
}

TEST(BugfixR2Series, LineSetXWithNaNIsUnsorted)
{
    LineSeries         s;
    std::vector<float> x{0.0f, 1.0f, 2.0f};
    s.set_x(x);
    EXPECT_TRUE(s.x_is_sorted());
    std::vector<float> xn{0.0f, kNaN, 2.0f};
    s.set_x(xn);
    EXPECT_FALSE(s.x_is_sorted());
}

TEST(BugfixR2Series, LineAppendNaNIsUnsorted)
{
    LineSeries s;
    s.append(0.0f, 0.0f);
    s.append(1.0f, 1.0f);
    EXPECT_TRUE(s.x_is_sorted());
    s.append(kNaN, 0.0f);
    EXPECT_FALSE(s.x_is_sorted());
    s.append(2.0f, 0.0f);
    EXPECT_FALSE(s.x_is_sorted());
}

TEST(BugfixR2Series, LineAppendLeadingNaNIsUnsorted)
{
    LineSeries s;
    s.append(kNaN, 0.0f);
    EXPECT_FALSE(s.x_is_sorted());
}

TEST(BugfixR2Series, ScatterNaNIsUnsorted)
{
    std::vector<float> x{0.0f, kNaN, 1.0f};
    std::vector<float> y{0.0f, 1.0f, 2.0f};
    ScatterSeries      s(x, y);
    EXPECT_FALSE(s.x_is_sorted());

    ScatterSeries s2;
    s2.append(0.0f, 0.0f);
    s2.append(kNaN, 0.0f);
    EXPECT_FALSE(s2.x_is_sorted());
}

TEST(BugfixR2Series, PendingReplaceWithNaNIsUnsorted)
{
    LineSeries s;
    s.set_thread_safe(true);
    std::vector<float> x{0.0f, kNaN, 2.0f};
    std::vector<float> y{0.0f, 1.0f, 2.0f};
    s.set_x(x);
    s.set_y(y);
    EXPECT_TRUE(s.commit_pending());
    EXPECT_FALSE(s.x_is_sorted());
}

TEST(BugfixR2Series, PendingAppendWithNaNIsUnsorted)
{
    LineSeries s;
    s.set_thread_safe(true);
    s.append(0.0f, 0.0f);
    s.append(1.0f, 1.0f);
    EXPECT_TRUE(s.commit_pending());
    EXPECT_TRUE(s.x_is_sorted());

    s.append(2.0f, 0.0f);
    s.append(kNaN, 0.0f);
    s.append(3.0f, 0.0f);
    EXPECT_TRUE(s.commit_pending());
    EXPECT_FALSE(s.x_is_sorted());
}

TEST(BugfixR2Series, PendingAppendNaNAtBoundaryIsUnsorted)
{
    ScatterSeries s;
    s.set_thread_safe(true);
    s.append(0.0f, 0.0f);
    EXPECT_TRUE(s.commit_pending());
    s.append(kNaN, 0.0f);
    EXPECT_TRUE(s.commit_pending());
    EXPECT_FALSE(s.x_is_sorted());
}

// ── Bug #54 follow-up: MeshSeries partial vertices ──────────────────────────

TEST(BugfixR2Series, MeshSetVerticesTruncatesPartialVertex)
{
    // One full vertex + 4 stray floats.
    std::vector<float> v{1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 1.0f, 9.0f, 9.0f, 9.0f, 9.0f};
    MeshSeries         m;
    m.set_vertices(v);
    EXPECT_EQ(m.vertices().size(), 6u);
    EXPECT_EQ(m.vertex_count(), 1u);
}

TEST(BugfixR2Series, MeshFewerThanOneVertexHasEmptyBoundsAndZeroCentroid)
{
    std::vector<float>    v{5.0f, 6.0f, 7.0f};
    std::vector<uint32_t> idx{0, 0, 0};
    MeshSeries            m(v, idx);
    EXPECT_TRUE(m.vertices().empty());

    vec3 mn{1.0f, 1.0f, 1.0f};
    vec3 mx{1.0f, 1.0f, 1.0f};
    m.get_bounds(mn, mx);
    EXPECT_EQ(mn, (vec3{0.0f, 0.0f, 0.0f}));
    EXPECT_EQ(mx, (vec3{0.0f, 0.0f, 0.0f}));

    vec3 c = m.compute_centroid();
    EXPECT_FALSE(std::isnan(c.x));
    EXPECT_EQ(c, (vec3{0.0f, 0.0f, 0.0f}));
}

TEST(BugfixR2Series, MeshBoundsAndCentroidIgnoreTrailingPartial)
{
    std::vector<float> v{0.0f,
                         0.0f,
                         0.0f,
                         0.0f,
                         0.0f,
                         1.0f,
                         2.0f,
                         4.0f,
                         6.0f,
                         0.0f,
                         0.0f,
                         1.0f,
                         100.0f,
                         100.0f,
                         100.0f};
    MeshSeries         m;
    m.set_vertices(v);
    vec3 mn, mx;
    m.get_bounds(mn, mx);
    EXPECT_EQ(mn, (vec3{0.0f, 0.0f, 0.0f}));
    EXPECT_EQ(mx, (vec3{2.0f, 4.0f, 6.0f}));
    EXPECT_EQ(m.compute_centroid(), (vec3{1.0f, 2.0f, 3.0f}));
}
