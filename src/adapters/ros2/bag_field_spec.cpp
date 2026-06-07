#include "bag_field_spec.hpp"

#include <algorithm>

namespace spectra::adapters::ros2
{

namespace
{

void set_column_name(BagFieldSpec& spec)
{
    spec.column_name = spec.topic + "/" + spec.field_path;
}

}   // namespace

bool parse_bag_field_spec(const std::string& token, BagFieldSpec& out)
{
    if (token.empty() || token[0] != '/')
        return false;

    const auto colon = token.find(':');
    if (colon != std::string::npos && colon > 0)
    {
        out.topic      = token.substr(0, colon);
        out.field_path = token.substr(colon + 1);
        if (out.topic.empty() || out.field_path.empty())
            return false;
        set_column_name(out);
        return true;
    }

    const auto dot = token.find('.', 1);
    if (dot == std::string::npos || dot + 1 >= token.size())
        return false;

    out.topic      = token.substr(0, dot);
    out.field_path = token.substr(dot + 1);
    set_column_name(out);
    return !out.topic.empty() && !out.field_path.empty();
}

bool resolve_bag_field_spec(const std::string&              token,
                            const std::vector<std::string>& bag_topics,
                            BagFieldSpec&                   out)
{
    BagFieldSpec direct;
    if (parse_bag_field_spec(token, direct))
    {
        for (const auto& topic : bag_topics)
        {
            if (topic == direct.topic)
            {
                out = direct;
                return true;
            }
        }
    }

    std::string best_topic;
    for (const auto& topic : bag_topics)
    {
        if (token.size() > topic.size() && token.compare(0, topic.size(), topic) == 0
            && token[topic.size()] == '.' && topic.size() > best_topic.size())
        {
            best_topic = topic;
        }
    }

    if (best_topic.empty())
        return false;

    out.topic      = best_topic;
    out.field_path = token.substr(best_topic.size() + 1);
    set_column_name(out);
    return !out.field_path.empty();
}

}   // namespace spectra::adapters::ros2
