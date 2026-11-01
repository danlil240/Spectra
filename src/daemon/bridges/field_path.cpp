#include "field_path.hpp"

namespace spectra::daemon
{

std::string join_topic_path(std::string_view prefix, std::string_view path)
{
    std::string out;
    out.reserve(prefix.size() + path.size() + 1);

    auto append_part = [&out](std::string_view part)
    {
        for (char c : part)
        {
            const auto uc = static_cast<unsigned char>(c);
            if (c == '/')
            {
                // Collapse repeated separators and drop a leading one.
                if (out.empty() || out.back() == '/')
                    continue;
                out.push_back('/');
            }
            else if (uc < 0x20 || uc == 0x7F)
            {
                out.push_back('_');
            }
            else
            {
                out.push_back(c);
            }
        }
    };

    append_part(prefix);
    if (!out.empty() && out.back() != '/' && !path.empty())
        out.push_back('/');
    append_part(path);

    while (!out.empty() && out.back() == '/')
        out.pop_back();
    if (out.empty() || out.size() > MAX_BRIDGE_TOPIC_NAME)
        return {};
    return out;
}

bool is_timestamp_field(std::string_view path)
{
    return path == "t" || path == "timestamp";
}

void fan_out_message(const std::vector<FlatField>&  fields,
                     std::string_view               prefix,
                     double                         default_time,
                     std::vector<TopicFieldSample>& out,
                     size_t*                        skipped)
{
    double x = default_time;
    for (const auto& f : fields)
    {
        if (is_timestamp_field(f.path))
        {
            x = f.value;
            break;
        }
    }

    for (const auto& f : fields)
    {
        if (is_timestamp_field(f.path))
            continue;
        std::string name = join_topic_path(prefix, f.path);
        if (name.empty())
        {
            if (skipped)
                ++*skipped;
            continue;
        }
        TopicFieldSample s;
        s.name = std::move(name);
        s.x    = x;
        s.y    = f.value;
        out.push_back(std::move(s));
    }
}

}   // namespace spectra::daemon
