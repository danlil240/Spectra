#pragma once

// Pure (ImGui-free) helpers shared by the PX4 panels so their logic can be
// unit tested without an ImGui context: parameter-table sorting and stable
// widget labels for rows whose display text changes every frame.

#include "../ulog_reader.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace spectra::adapters::px4
{

// Columns of the ULog Parameters table, in display order.
enum class ParamSortColumn
{
    Name  = 0,
    Type  = 1,
    Value = 2,
};

struct ParamSortKey
{
    ParamSortColumn column{ParamSortColumn::Name};
    bool            ascending{true};
};

// Numeric value of a parameter regardless of its stored type.
double parameter_numeric_value(const ULogParameter& param) noexcept;

// Display name of a parameter's type ("float" / "int32").
const char* parameter_type_name(const ULogParameter& param) noexcept;

// Returns row indices into `params` ordered by `keys` (primary key first).
// Values compare numerically; ties fall back to the original order, so the
// sort is stable.  An empty `keys` span yields the identity order.
std::vector<size_t> sorted_parameter_indices(std::span<const ULogParameter> params,
                                             std::span<const ParamSortKey>  keys);

// ImGui label "<display>###<id>": ImGui hashes only the part after "###",
// so the widget keeps its identity (hover, activation, open state) while the
// visible text changes.
std::string stable_label(std::string_view display, std::string_view id);

// Tree-node label for a live telemetry channel, e.g. "ATTITUDE (7 fields)".
std::string live_channel_label(std::string_view channel, size_t field_count);

// Selectable label for a live telemetry field, e.g. "roll = 0.1234".
std::string live_field_label(std::string_view field, double value);

}   // namespace spectra::adapters::px4
