#include <spectra/chunked_series.hpp>

#include <algorithm>
#include <cassert>
#include <cstring>

#include "data/chunked_array.hpp"
#include "data/decimation.hpp"
#include "data/lod_cache.hpp"
#include "data/mapped_file.hpp"

namespace spectra
{

struct ChunkedLineSeries::Impl
{
    data::ChunkedArray x;
    data::ChunkedArray y;
    data::LodCache     lod;

    bool        lod_enabled    = false;
    std::size_t memory_budget  = 0;   // 0 = unlimited
    float       line_width     = 2.0f;
    bool        lod_dirty      = true;
    uint64_t    lod_generation = 0;

    Impl() = default;
    explicit Impl(std::size_t chunk_size) : x(chunk_size), y(chunk_size) {}
};

ChunkedLineSeries::ChunkedLineSeries() : impl_(std::make_unique<Impl>()) {}

ChunkedLineSeries::~ChunkedLineSeries() = default;

ChunkedLineSeries::ChunkedLineSeries(ChunkedLineSeries&&) noexcept = default;

ChunkedLineSeries& ChunkedLineSeries::operator=(ChunkedLineSeries&&) noexcept = default;

// ── Data ingestion ──

ChunkedLineSeries& ChunkedLineSeries::set_data(std::span<const float> x,
                                               std::span<const float> y)
{
    assert(x.size() == y.size());
    impl_->x.clear();
    impl_->y.clear();
    impl_->x.append(x);
    impl_->y.append(y);
    impl_->lod_dirty = true;
    dirty_            = true;
    return *this;
}

ChunkedLineSeries& ChunkedLineSeries::load_binary(const std::string& path,
                                                   std::size_t        offset_bytes,
                                                   std::size_t        count)
{
    data::MappedFile file(path);
    if (!file.is_open())
        return *this;

    std::size_t avail_bytes = (file.size() > offset_bytes) ? file.size() - offset_bytes : 0;
    std::size_t avail_pairs = avail_bytes / (2 * sizeof(float));
    if (count == 0 || count > avail_pairs)
        count = avail_pairs;

    if (count == 0)
        return *this;

    auto interleaved = file.subspan_float(offset_bytes, count * 2);

    impl_->x.clear();
    impl_->y.clear();

    // De-interleave
    for (std::size_t i = 0; i < count; ++i)
    {
        impl_->x.push_back(interleaved[i * 2]);
        impl_->y.push_back(interleaved[i * 2 + 1]);
    }

    impl_->lod_dirty = true;
    dirty_            = true;
    return *this;
}

ChunkedLineSeries& ChunkedLineSeries::load_binary_columns(const std::string& path,
                                                           std::size_t        x_offset_bytes,
                                                           std::size_t        y_offset_bytes,
                                                           std::size_t        count)
{
    data::MappedFile file(path);
    if (!file.is_open())
        return *this;

    auto x_span = file.subspan_float(x_offset_bytes, count);
    auto y_span = file.subspan_float(y_offset_bytes, count);

    std::size_t actual = std::min(x_span.size(), y_span.size());
    if (actual == 0)
        return *this;

    impl_->x.clear();
    impl_->y.clear();
    impl_->x.append(x_span.subspan(0, actual));
    impl_->y.append(y_span.subspan(0, actual));

    impl_->lod_dirty = true;
    dirty_            = true;
    return *this;
}

void ChunkedLineSeries::append(float x, float y)
{
    impl_->x.push_back(x);
    impl_->y.push_back(y);
    impl_->lod_dirty = true;
    dirty_            = true;
    enforce_memory_budget();
}

void ChunkedLineSeries::append_batch(std::span<const float> x, std::span<const float> y)
{
    assert(x.size() == y.size());
    impl_->x.append(x);
    impl_->y.append(y);
    impl_->lod_dirty = true;
    dirty_            = true;
    enforce_memory_budget();
}

std::size_t ChunkedLineSeries::erase_before(float x_threshold)
{
    if (impl_->x.empty())
        return 0;

    // Binary search for the first element >= x_threshold.
    std::size_t lo = 0, hi = impl_->x.size();
    while (lo < hi)
    {
        std::size_t mid = lo + (hi - lo) / 2;
        if (impl_->x[mid] < x_threshold)
            lo = mid + 1;
        else
            hi = mid;
    }

    if (lo == 0)
        return 0;

    impl_->x.erase_front(lo);
    impl_->y.erase_front(lo);
    impl_->lod_dirty = true;
    dirty_            = true;
    return lo;
}

// ── Configuration ──

ChunkedLineSeries& ChunkedLineSeries::enable_lod(bool enable)
{
    impl_->lod_enabled = enable;
    if (enable)
        impl_->lod_dirty = true;
    return *this;
}

bool ChunkedLineSeries::lod_enabled() const
{
    return impl_->lod_enabled;
}

ChunkedLineSeries& ChunkedLineSeries::set_memory_budget(std::size_t bytes)
{
    impl_->memory_budget = bytes;
    enforce_memory_budget();
    return *this;
}

std::size_t ChunkedLineSeries::memory_budget() const
{
    return impl_->memory_budget;
}

ChunkedLineSeries& ChunkedLineSeries::set_chunk_size(std::size_t size)
{
    // Rebuild arrays with new chunk size
    if (size == 0)
        size = data::ChunkedArray::DEFAULT_CHUNK_SIZE;

    if (impl_->x.chunk_size() == size)
        return *this;

    auto old_x = impl_->x.read_vec(0, impl_->x.size());
    auto old_y = impl_->y.read_vec(0, impl_->y.size());

    impl_->x = data::ChunkedArray(old_x, size);
    impl_->y = data::ChunkedArray(old_y, size);
    return *this;
}

ChunkedLineSeries& ChunkedLineSeries::width(float w)
{
    impl_->line_width = w;
    dirty_             = true;
    return *this;
}

float ChunkedLineSeries::width() const
{
    return impl_->line_width;
}

// ── Data access ──

std::size_t ChunkedLineSeries::point_count() const
{
    return impl_->x.size();
}

std::size_t ChunkedLineSeries::memory_bytes() const
{
    return impl_->x.memory_bytes() + impl_->y.memory_bytes() + impl_->lod.memory_bytes();
}

std::vector<float> ChunkedLineSeries::x_range(std::size_t offset, std::size_t count) const
{
    return impl_->x.read_vec(offset, count);
}

std::vector<float> ChunkedLineSeries::y_range(std::size_t offset, std::size_t count) const
{
    return impl_->y.read_vec(offset, count);
}

ChunkedLineSeries::VisibleData ChunkedLineSeries::visible_data(float       x_min,
                                                                float       x_max,
                                                                std::size_t max_points) const
{
    if (impl_->x.empty())
        return {};

    // Get full data as vectors for range queries
    // For very large datasets, this could be optimized to avoid the copy,
    // but the LoD cache amortizes the cost.
    auto all_x = impl_->x.read_vec(0, impl_->x.size());
    auto all_y = impl_->y.read_vec(0, impl_->y.size());

    std::span<const float> x_span(all_x);
    std::span<const float> y_span(all_y);

    // Try LoD cache first
    if (impl_->lod_enabled)
    {
        rebuild_lod_if_needed();
        if (!impl_->lod.empty())
        {
            auto result = impl_->lod.query(x_span, y_span, x_min, x_max, max_points);
            return {
                std::vector<float>(result.x.begin(), result.x.end()),
                std::vector<float>(result.y.begin(), result.y.end()),
                result.level,
            };
        }
    }

    // No LoD — clip to visible range and decimate if needed
    auto lo_it =
        std::lower_bound(all_x.begin(), all_x.end(), x_min);
    auto hi_it =
        std::upper_bound(all_x.begin(), all_x.end(), x_max);

    auto lo_idx = static_cast<std::size_t>(lo_it - all_x.begin());
    auto hi_idx = static_cast<std::size_t>(hi_it - all_x.begin());

    std::size_t visible_count = hi_idx - lo_idx;
    if (visible_count == 0)
        return {};

    std::span<const float> vis_x = x_span.subspan(lo_idx, visible_count);
    std::span<const float> vis_y = y_span.subspan(lo_idx, visible_count);

    if (visible_count <= max_points)
    {
        return {
            std::vector<float>(vis_x.begin(), vis_x.end()),
            std::vector<float>(vis_y.begin(), vis_y.end()),
            0,
        };
    }

    // Decimate to fit budget
    auto decimated = data::min_max_decimate(vis_x, vis_y, max_points / 2);
    VisibleData out;
    out.x.reserve(decimated.size());
    out.y.reserve(decimated.size());
    for (auto& [dx, dy] : decimated)
    {
        out.x.push_back(dx);
        out.y.push_back(dy);
    }
    out.lod_level = 0;   // Ad-hoc decimation, not a cached level
    return out;
}

// ── Fluent setters ──

ChunkedLineSeries& ChunkedLineSeries::format(std::string_view fmt)
{
    Series::apply_format_string(fmt);
    return *this;
}

// ── Private ──

void ChunkedLineSeries::rebuild_lod_if_needed() const
{
    if (!impl_->lod_dirty)
        return;

    auto all_x = impl_->x.read_vec(0, impl_->x.size());
    auto all_y = impl_->y.read_vec(0, impl_->y.size());

    // const_cast is safe here: we're updating a cache, not logical state.
    auto& lod     = const_cast<data::LodCache&>(impl_->lod);
    auto& dirty   = const_cast<bool&>(impl_->lod_dirty);
    lod.build(all_x, all_y);
    dirty = false;
}

void ChunkedLineSeries::enforce_memory_budget()
{
    if (impl_->memory_budget == 0)
        return;

    while (impl_->x.memory_bytes() + impl_->y.memory_bytes() > impl_->memory_budget
           && impl_->x.size() > 0)
    {
        // Drop one chunk worth of data from the front
        std::size_t drop = std::min(impl_->x.chunk_size(), impl_->x.size());
        impl_->x.erase_front(drop);
        impl_->y.erase_front(drop);
    }
}

}   // namespace spectra
