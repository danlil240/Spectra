#pragma once

// data_binding.hpp — layout data-binding URIs.
//
// A layout file (File > Save Layout) stores, for every series, where its data
// came from instead of the samples themselves.  The source is a URI:
//
//   topic://<topic name>                  live Spectra topic, e.g. topic://imu/accel_x
//   csv://<file name>[?x=<column>]#<column>
//                                         y column of a CSV file (x column optional;
//                                         without it x is the row index)
//   ulog://<topic>.<field>                PX4 ULog field (reserved: parsed and saved,
//                                         not resolved yet)
//
// Components are percent-encoded where needed ('%', '#', '?', '&', '=' and
// control characters), so file and column names may contain any character.
// The CSV file is stored by name only, so a teammate's copy in another
// directory still matches.  See docs/layouts.md.

#include <optional>
#include <string>
#include <string_view>

namespace spectra
{

enum class BindingScheme
{
    Topic,
    Csv,
    Ulog,
};

struct DataBinding
{
    BindingScheme scheme = BindingScheme::Topic;
    // topic: topic name.  csv: file name.  ulog: ULog topic (message) name.
    std::string source;
    // csv: y column.  ulog: field path.  topic: unused.
    std::string field;
    // csv: x column ("" = row index).  Unused otherwise.
    std::string x_field;

    bool operator==(const DataBinding&) const = default;
};

// URI scheme name without "://" ("topic", "csv", "ulog").
const char* binding_scheme_name(BindingScheme scheme);

// Parses a binding URI.  Returns nullopt (and a reason in *error) for an empty
// or malformed URI or an unknown scheme.
std::optional<DataBinding> parse_data_binding(std::string_view uri, std::string* error = nullptr);

// Formats a binding as a URI; parse_data_binding(format_data_binding(b)) == b.
std::string format_data_binding(const DataBinding& binding);

// Shorthands for the bindings recorded when series are created.
std::string topic_binding_uri(std::string_view topic_name);
std::string csv_binding_uri(std::string_view file_name,
                            std::string_view x_column,
                            std::string_view y_column);

}   // namespace spectra
