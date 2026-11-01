#include "log_reader.hpp"

#include <algorithm>
#include <cctype>

namespace spectra::logs
{

std::string LogField::full_name() const
{
    if (path.empty())
        return topic;
    return topic + "/" + path;
}

bool LogReader::read_range(size_t field_index, double t0, double t1, LogSeries& out) const
{
    LogSeries all;
    if (!read(field_index, all))
        return false;

    // Times are non-decreasing, so the range is one contiguous slice.
    const auto first = std::lower_bound(all.time.begin(), all.time.end(), t0);
    const auto last  = std::upper_bound(first, all.time.end(), t1);
    const auto lo    = first - all.time.begin();
    const auto hi    = last - all.time.begin();
    out.time.assign(first, last);
    out.values.assign(all.values.begin() + lo, all.values.begin() + hi);
    return true;
}

std::optional<size_t> LogReader::find_field(std::string_view topic, std::string_view path) const
{
    const auto& all = fields();
    for (size_t i = 0; i < all.size(); ++i)
    {
        if (all[i].topic == topic && all[i].path == path)
            return i;
    }
    return std::nullopt;
}

namespace
{

LogFieldNode& group_named(LogFieldNode& parent, std::string_view name)
{
    // Fields of one topic arrive together, so the matching group is almost
    // always the most recent sibling: scan from the back.
    for (auto it = parent.children.rbegin(); it != parent.children.rend(); ++it)
    {
        if (it->field_index < 0 && it->name == name)
            return *it;
    }
    parent.children.push_back(LogFieldNode{std::string(name), -1, 0, {}});
    return parent.children.back();
}

char ascii_lower(char c)
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

}   // namespace

LogFieldNode build_field_tree(const std::vector<LogField>& fields)
{
    LogFieldNode root;
    for (size_t i = 0; i < fields.size(); ++i)
    {
        const LogField& f    = fields[i];
        LogFieldNode*   node = &group_named(root, f.topic);
        node->sample_count   = std::max(node->sample_count, f.sample_count);

        std::string_view rest = f.path;
        while (true)
        {
            const size_t dot = rest.find('.');
            if (dot == std::string_view::npos)
            {
                // A topic that is itself a scalar has an empty path.
                std::string leaf = rest.empty() ? std::string("value") : std::string(rest);
                node->children.push_back(
                    LogFieldNode{std::move(leaf), static_cast<int>(i), f.sample_count, {}});
                break;
            }
            node               = &group_named(*node, rest.substr(0, dot));
            node->sample_count = std::max(node->sample_count, f.sample_count);
            rest.remove_prefix(dot + 1);
        }
    }
    return root;
}

bool field_matches_filter(const LogField& field, std::string_view filter)
{
    if (filter.empty())
        return true;
    const std::string hay = field.full_name();
    const auto        it  = std::search(hay.begin(),
                                hay.end(),
                                filter.begin(),
                                filter.end(),
                                [](char a, char b) { return ascii_lower(a) == ascii_lower(b); });
    return it != hay.end();
}

}   // namespace spectra::logs
