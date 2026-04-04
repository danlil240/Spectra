#pragma once

#include <cstddef>
#include <span>
#include <string>

// Platform-specific includes handled in .cpp

namespace spectra::data
{

/// Cross-platform read-only memory-mapped file.
/// Maps an entire file into the process address space for zero-copy data access.
/// On POSIX systems uses mmap(); on Windows uses CreateFileMapping().
///
/// The mapped region is read-only and remains valid for the lifetime of this
/// object. The OS manages page eviction and demand-paging transparently.
class MappedFile
{
   public:
    /// Construct without mapping (empty state).
    MappedFile() = default;

    /// Map the file at `path`.  Throws std::runtime_error on failure.
    explicit MappedFile(const std::string& path);

    ~MappedFile();

    // Move-only (unique ownership of OS mapping handle).
    MappedFile(MappedFile&& other) noexcept;
    MappedFile& operator=(MappedFile&& other) noexcept;
    MappedFile(const MappedFile&)            = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    /// Whether a file is currently mapped.
    [[nodiscard]] bool is_open() const { return open_; }

    /// Raw pointer to the mapped region.
    [[nodiscard]] const void* data() const { return data_; }

    /// File size in bytes.
    [[nodiscard]] std::size_t size() const { return size_; }

    /// Typed subspan of the mapped region.  `byte_offset` and `count` are
    /// in units of float.  Returns an empty span if the range is out of bounds.
    [[nodiscard]] std::span<const float> subspan_float(std::size_t byte_offset,
                                                       std::size_t count) const;

    /// Close the mapping explicitly (also called by destructor).
    void close();

   private:
    void*       data_ = nullptr;
    std::size_t size_ = 0;
    bool        open_ = false;

#ifdef _WIN32
    void* file_handle_    = nullptr;   // HANDLE
    void* mapping_handle_ = nullptr;   // HANDLE
#else
    int fd_ = -1;
#endif
};

}   // namespace spectra::data
