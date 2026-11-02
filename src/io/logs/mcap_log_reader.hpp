#pragma once

// McapLogReader — LogReader for MCAP files (https://mcap.dev), built on the
// vendored foxglove/mcap C++ library (SPECTRA_USE_MCAP).
//
// Topics are MCAP channel topics.  Supported message encodings:
//   - "json": numbers and bools become fields, objects nest with ".",
//     arrays expand to "[i]"; strings and nulls are skipped.
//   - "cdr" with a "ros2msg" schema: decoded by ros2msg_cdr.hpp from the
//     schema text stored in the file, no ROS install needed.
// Other encodings are skipped and listed in warnings().  Timestamps are the
// messages' log times.  Chunk compression (zstd/lz4) is not supported in this
// build; such chunks are skipped with a warning.
//
// open() decodes the whole file once and keeps every field's samples in
// memory, so read() is a copy.  Arrays longer than
// Options::max_array_elements are not expanded.

#include "log_reader.hpp"

#include <cstdint>
#include <span>

namespace spectra::logs
{

class McapLogReader final : public LogReader
{
   public:
    struct Options
    {
        size_t max_array_elements = 64;
    };

    McapLogReader() = default;
    explicit McapLogReader(Options options) : options_(options) {}

    std::string_view format_name() const override { return "MCAP"; }

    bool                         open(const std::string& path) override;
    const std::vector<LogField>& fields() const override { return fields_; }
    bool                         read(size_t field_index, LogSeries& out) const override;

    // "\x89MCAP0\r\n".
    static bool has_magic(std::span<const uint8_t> head);

   private:
    Options                options_;
    std::vector<LogField>  fields_;
    std::vector<LogSeries> columns_;   // parallel to fields_
};

}   // namespace spectra::logs
