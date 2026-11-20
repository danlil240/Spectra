#include "px4_panel_helpers.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <numeric>

namespace spectra::adapters::px4
{

double parameter_numeric_value(const ULogParameter& param) noexcept
{
    return param.is_float ? static_cast<double>(param.value_float)
                          : static_cast<double>(param.value_int);
}

const char* parameter_type_name(const ULogParameter& param) noexcept
{
    return param.is_float ? "float" : "int32";
}

namespace
{

// Three-way compare of two parameters on one column.
int compare_on(const ULogParameter& a, const ULogParameter& b, ParamSortColumn column)
{
    switch (column)
    {
        case ParamSortColumn::Name:
        {
            const int c = a.key.compare(b.key);
            return (c < 0) ? -1 : (c > 0 ? 1 : 0);
        }
        case ParamSortColumn::Type:
        {
            const std::string_view ta = parameter_type_name(a);
            const std::string_view tb = parameter_type_name(b);
            return (ta < tb) ? -1 : (tb < ta ? 1 : 0);
        }
        case ParamSortColumn::Value:
        {
            const double va = parameter_numeric_value(a);
            const double vb = parameter_numeric_value(b);
            return (va < vb) ? -1 : (vb < va ? 1 : 0);
        }
    }
    return 0;
}

}   // namespace

std::vector<size_t> sorted_parameter_indices(std::span<const ULogParameter> params,
                                             std::span<const ParamSortKey>  keys)
{
    std::vector<size_t> order(params.size());
    std::iota(order.begin(), order.end(), size_t{0});
    if (keys.empty())
        return order;

    std::stable_sort(order.begin(),
                     order.end(),
                     [&](size_t lhs, size_t rhs)
                     {
                         for (const auto& key : keys)
                         {
                             const int c = compare_on(params[lhs], params[rhs], key.column);
                             if (c != 0)
                                 return key.ascending ? (c < 0) : (c > 0);
                         }
                         return false;
                     });
    return order;
}

std::string stable_label(std::string_view display, std::string_view id)
{
    return std::format("{}###{}", display, id);
}

std::string live_channel_label(std::string_view channel, size_t field_count)
{
    return stable_label(std::format("{} ({} fields)", channel, field_count), channel);
}

std::string live_field_label(std::string_view field, double value)
{
    return stable_label(std::format("{} = {:.4f}", field, value), field);
}

Px4FieldDragPayload make_field_drag_payload(std::string_view topic,
                                            std::string_view field,
                                            int              array_idx,
                                            bool             live)
{
    Px4FieldDragPayload p;
    const size_t        tn = std::min(topic.size(), sizeof(p.topic) - 1);
    const size_t        fn = std::min(field.size(), sizeof(p.field) - 1);
    std::copy_n(topic.data(), tn, p.topic);
    std::copy_n(field.data(), fn, p.field);
    p.array_idx = array_idx;
    p.live      = live;
    return p;
}

FieldKey parse_field_key(std::string_view key)
{
    const size_t open = key.find('[');
    if (open == std::string_view::npos)
        return {std::string(key), -1};
    const size_t close = key.find(']', open);
    int          idx   = -1;
    if (close != std::string_view::npos)
        std::from_chars(key.data() + open + 1, key.data() + close, idx);
    return {std::string(key.substr(0, open)), idx};
}

}   // namespace spectra::adapters::px4
