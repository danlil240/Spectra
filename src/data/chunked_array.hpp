#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <span>
#include <vector>

namespace spectra::data
{

/// Segmented array that stores data in fixed-size chunks instead of one
/// contiguous vector.  Avoids massive reallocations for very large datasets
/// and enables demand-loaded / memory-mapped data access.
///
/// Default chunk size: 1 048 576 elements (1M floats ≈ 4 MB per chunk).
class ChunkedArray
{
   public:
    static constexpr std::size_t DEFAULT_CHUNK_SIZE = 1u << 20;   // 1M elements

    /// Construct an empty chunked array with the given chunk size.
    explicit ChunkedArray(std::size_t chunk_size = DEFAULT_CHUNK_SIZE)
        : chunk_size_(chunk_size > 0 ? chunk_size : DEFAULT_CHUNK_SIZE)
    {
    }

    /// Construct from a contiguous span, splitting into chunks.
    ChunkedArray(std::span<const float> data, std::size_t chunk_size = DEFAULT_CHUNK_SIZE)
        : chunk_size_(chunk_size > 0 ? chunk_size : DEFAULT_CHUNK_SIZE)
    {
        append(data);
    }

    // Move-only for efficiency (chunks can be large).
    ChunkedArray(ChunkedArray&&) noexcept            = default;
    ChunkedArray& operator=(ChunkedArray&&) noexcept = default;
    ChunkedArray(const ChunkedArray&)                = default;
    ChunkedArray& operator=(const ChunkedArray&)     = default;

    /// Number of elements stored.
    [[nodiscard]] std::size_t size() const { return size_; }
    [[nodiscard]] bool        empty() const { return size_ == 0; }

    /// Number of chunks allocated.
    [[nodiscard]] std::size_t chunk_count() const { return chunks_.size(); }

    /// Chunk capacity (elements per chunk).
    [[nodiscard]] std::size_t chunk_size() const { return chunk_size_; }

    /// Total memory usage in bytes (all chunks, including unused capacity).
    [[nodiscard]] std::size_t memory_bytes() const
    {
        std::size_t total = 0;
        for (auto& c : chunks_)
            total += c.capacity() * sizeof(float);
        return total;
    }

    /// Element access (no bounds check in release).
    [[nodiscard]] float operator[](std::size_t index) const
    {
        assert(index < size_);
        return chunks_[index / chunk_size_][index % chunk_size_];
    }

    float& operator[](std::size_t index)
    {
        assert(index < size_);
        return chunks_[index / chunk_size_][index % chunk_size_];
    }

    /// Read a contiguous range of elements into `dest`.
    /// Returns the number of elements actually read (may be less than `count`
    /// if the range extends past the end).
    std::size_t read(std::size_t offset, std::size_t count, float* dest) const
    {
        if (offset >= size_)
            return 0;
        count = std::min(count, size_ - offset);

        std::size_t remaining = count;
        std::size_t src_off   = offset;

        while (remaining > 0)
        {
            std::size_t ci    = src_off / chunk_size_;
            std::size_t co    = src_off % chunk_size_;
            std::size_t avail = std::min(chunk_size_ - co, remaining);

            std::memcpy(dest, chunks_[ci].data() + co, avail * sizeof(float));
            dest += avail;
            src_off += avail;
            remaining -= avail;
        }
        return count;
    }

    /// Read a range into a new vector. Convenience wrapper around read().
    [[nodiscard]] std::vector<float> read_vec(std::size_t offset, std::size_t count) const
    {
        if (offset >= size_)
            return {};
        count = std::min(count, size_ - offset);
        std::vector<float> out(count);
        read(offset, count, out.data());
        return out;
    }

    /// Append a single element.
    void push_back(float value)
    {
        ensure_capacity(size_ + 1);
        chunks_[size_ / chunk_size_][size_ % chunk_size_] = value;
        ++size_;
    }

    /// Append a span of elements.
    void append(std::span<const float> data)
    {
        if (data.empty())
            return;

        ensure_capacity(size_ + data.size());
        std::size_t remaining = data.size();
        const float* src      = data.data();

        while (remaining > 0)
        {
            std::size_t ci    = size_ / chunk_size_;
            std::size_t co    = size_ % chunk_size_;
            std::size_t space = chunk_size_ - co;
            std::size_t n     = std::min(space, remaining);

            std::memcpy(chunks_[ci].data() + co, src, n * sizeof(float));
            src += n;
            size_ += n;
            remaining -= n;
        }
    }

    /// Remove all elements.
    void clear()
    {
        chunks_.clear();
        size_ = 0;
    }

    /// Remove elements before the given index.
    /// Returns the number of elements removed.
    std::size_t erase_front(std::size_t count)
    {
        if (count == 0 || size_ == 0)
            return 0;
        count = std::min(count, size_);

        // Calculate how many complete chunks to drop
        std::size_t full_chunks_to_drop = count / chunk_size_;

        if (full_chunks_to_drop > 0)
        {
            chunks_.erase(chunks_.begin(),
                          chunks_.begin() + static_cast<ptrdiff_t>(full_chunks_to_drop));
        }

        std::size_t leftover = count % chunk_size_;
        if (leftover > 0 && !chunks_.empty())
        {
            chunks_[0].erase(chunks_[0].begin(),
                             chunks_[0].begin() + static_cast<ptrdiff_t>(leftover));
        }

        size_ -= count;
        return count;
    }

    /// Provide direct access to a chunk's data (for zero-copy upload).
    [[nodiscard]] std::span<const float> chunk_data(std::size_t chunk_index) const
    {
        assert(chunk_index < chunks_.size());
        const auto& chunk = chunks_[chunk_index];
        // Last chunk may be partial
        if (chunk_index == chunks_.size() - 1 && size_ % chunk_size_ != 0)
            return {chunk.data(), size_ % chunk_size_};
        return {chunk.data(), chunk.size()};
    }

   private:
    void ensure_capacity(std::size_t required)
    {
        std::size_t needed_chunks = (required + chunk_size_ - 1) / chunk_size_;
        while (chunks_.size() < needed_chunks)
        {
            chunks_.emplace_back(chunk_size_);
        }
    }

    std::vector<std::vector<float>> chunks_;
    std::size_t                     chunk_size_;
    std::size_t                     size_ = 0;
};

}   // namespace spectra::data
