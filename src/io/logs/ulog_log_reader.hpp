#pragma once

// ULogLogReader — LogReader over the PX4 ULog parser (ulog_reader.hpp).
//
// Topics are the logged subscriptions ("vehicle_attitude", or
// "vehicle_attitude[1]" for multi-instance topics).  Every numeric or bool
// field becomes a LogField; arrays expand to "name[i]" and nested message
// types to "name.sub" paths.  The per-message "timestamp" field is the time
// axis and padding/char fields are skipped.

#include "log_reader.hpp"
#include "ulog_reader.hpp"

#include <cstdint>
#include <span>

namespace spectra::logs
{

class ULogLogReader final : public LogReader
{
   public:
    std::string_view format_name() const override { return "ULog"; }

    bool                         open(const std::string& path) override;
    const std::vector<LogField>& fields() const override { return fields_; }
    bool                         read(size_t field_index, LogSeries& out) const override;

    // The underlying parser (metadata, parameters, log messages).
    const ULogReader& ulog() const { return reader_; }

    // "ULog" 0x01 0x12 0x35.
    static bool has_magic(std::span<const uint8_t> head);

   private:
    struct Slot
    {
        const ULogTimeSeries* series = nullptr;
        size_t                offset = 0;   // byte offset into ULogDataRow::payload
        ULogFieldType         type   = ULogFieldType::Float;
    };

    void add_fields(const std::string&       topic,
                    const ULogTimeSeries&    series,
                    const ULogMessageFormat& format,
                    const std::string&       prefix,
                    size_t                   base_offset,
                    int                      depth);

    ULogReader            reader_;
    std::vector<LogField> fields_;
    std::vector<Slot>     slots_;   // parallel to fields_
};

}   // namespace spectra::logs
