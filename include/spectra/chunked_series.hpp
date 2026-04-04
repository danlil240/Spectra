#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <spectra/series.hpp>
#include <string>
#include <vector>

namespace spectra::data
{
class ChunkedArray;
class MappedFile;
class LodCache;
}   // namespace spectra::data

namespace spectra
{

/// A line series backed by chunked storage and optional level-of-detail cache.
///
/// Unlike `LineSeries` which stores all data in contiguous `std::vector<float>`,
/// `ChunkedLineSeries` stores data in fixed-size chunks, enabling:
///   - Datasets larger than available RAM (with memory-mapped backing)
///   - Efficient streaming append without reallocation
///   - Multi-resolution rendering via an LoD decimation pyramid
///   - Rolling-window memory budgets for long-running streams
///
/// The renderer automatically detects this type and uploads only the visible,
/// decimated subset to the GPU instead of the entire dataset.
class ChunkedLineSeries : public Series
{
   public:
    ChunkedLineSeries();
    ~ChunkedLineSeries() override;

    // Non-copyable, non-movable (Series base has std::atomic<bool>).
    // Managed by pointer/unique_ptr; pimpl hides heavy state.
    ChunkedLineSeries(const ChunkedLineSeries&)            = delete;
    ChunkedLineSeries& operator=(const ChunkedLineSeries&) = delete;
    ChunkedLineSeries(ChunkedLineSeries&&)                 = delete;
    ChunkedLineSeries& operator=(ChunkedLineSeries&&)      = delete;

    // ── Data ingestion ──

    /// Set data from contiguous spans (copies into chunked storage).
    ChunkedLineSeries& set_data(std::span<const float> x, std::span<const float> y);

    /// Load interleaved [x0,y0,x1,y1,...] float data from a binary file.
    /// `offset_bytes` is the byte offset into the file where data begins.
    /// `count` is the number of (x,y) pairs to load (0 = all available).
    ChunkedLineSeries& load_binary(const std::string& path,
                                   std::size_t        offset_bytes = 0,
                                   std::size_t        count        = 0);

    /// Load from separate x and y columns in a binary file (column-major layout).
    /// Each column is `count` contiguous floats starting at the given byte offset.
    ChunkedLineSeries& load_binary_columns(const std::string& path,
                                           std::size_t        x_offset_bytes,
                                           std::size_t        y_offset_bytes,
                                           std::size_t        count);

    /// Append a single point (streaming).
    void append(float x, float y) override;

    /// Append a batch of points.
    void append_batch(std::span<const float> x, std::span<const float> y);

    /// Remove all points with x < x_threshold (assumes x sorted ascending).
    /// Returns the number of points removed.
    std::size_t erase_before(float x_threshold) override;

    // ── Configuration ──

    /// Enable/disable level-of-detail cache.  When enabled, a decimation
    /// pyramid is built on next data change, allowing the renderer to use
    /// a coarser version when zoomed out.
    ChunkedLineSeries& enable_lod(bool enable);
    [[nodiscard]] bool lod_enabled() const;

    /// Set a memory budget for streaming mode.  When the total data exceeds
    /// this budget, the oldest chunks are dropped.  0 = unlimited (default).
    ChunkedLineSeries&        set_memory_budget(std::size_t bytes);
    [[nodiscard]] std::size_t memory_budget() const;

    /// Set the chunk size (number of floats per chunk).
    ChunkedLineSeries& set_chunk_size(std::size_t size);

    /// Line width.
    ChunkedLineSeries&  width(float w);
    [[nodiscard]] float width() const;

    // ── Data access ──

    /// Total number of data points.
    [[nodiscard]] std::size_t point_count() const;

    /// Estimated memory consumption in bytes (x + y chunked arrays).
    [[nodiscard]] std::size_t memory_bytes() const override;

    /// Read a range of x values into a vector.
    [[nodiscard]] std::vector<float> x_range(std::size_t offset, std::size_t count) const;
    [[nodiscard]] std::vector<float> y_range(std::size_t offset, std::size_t count) const;

    /// Query visible data for the given x-range, returning at most `max_points`.
    /// Uses the LoD cache if available, otherwise falls back to min-max decimation.
    struct VisibleData
    {
        std::vector<float> x;
        std::vector<float> y;
        std::size_t        lod_level = 0;   // 0 = full resolution
    };

    [[nodiscard]] VisibleData visible_data(float       x_min,
                                           float       x_max,
                                           std::size_t max_points = 4096) const;

    // ── Fluent setters (return correct type) ──

    using Series::color;
    using Series::label;
    using Series::line_style;
    using Series::marker_size;
    using Series::marker_style;
    using Series::opacity;

    ChunkedLineSeries& label(const std::string& lbl)
    {
        Series::label(lbl);
        return *this;
    }
    ChunkedLineSeries& color(const Color& c)
    {
        Series::color(c);
        return *this;
    }
    ChunkedLineSeries& line_style(LineStyle s)
    {
        Series::line_style(s);
        return *this;
    }
    ChunkedLineSeries& marker_style(MarkerStyle s)
    {
        Series::marker_style(s);
        return *this;
    }
    ChunkedLineSeries& marker_size(float s)
    {
        Series::marker_size(s);
        return *this;
    }
    ChunkedLineSeries& opacity(float o)
    {
        Series::opacity(o);
        return *this;
    }
    ChunkedLineSeries& format(std::string_view fmt);

   private:
    void rebuild_lod_if_needed() const;
    void enforce_memory_budget();

    struct Impl;
    mutable std::unique_ptr<Impl> impl_;
};

}   // namespace spectra
