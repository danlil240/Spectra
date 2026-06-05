#include "settings_store.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ui/theme/theme.hpp"

namespace spectra::ui::settings
{

namespace
{

std::string escape_json(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s)
    {
        switch (c)
        {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += c;
                break;
        }
    }
    return out;
}

std::string read_json_string(const std::string& json, const std::string& key)
{
    std::string search = "\"" + key + "\"";
    auto        pos    = json.find(search);
    if (pos == std::string::npos)
        return {};
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos)
        return {};
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos)
        return {};
    size_t end = pos + 1;
    while (end < json.size())
    {
        if (json[end] == '"' && json[end - 1] != '\\')
            break;
        ++end;
    }
    return json.substr(pos + 1, end - pos - 1);
}

bool read_json_bool(const std::string& json, const std::string& key, bool def)
{
    std::string search = "\"" + key + "\"";
    auto        pos    = json.find(search);
    if (pos == std::string::npos)
        return def;
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos)
        return def;
    auto   rest  = json.substr(pos + 1, 10);
    size_t start = rest.find_first_not_of(" \t\n\r");
    if (start == std::string::npos)
        return def;
    if (rest.substr(start, 4) == "true")
        return true;
    if (rest.substr(start, 5) == "false")
        return false;
    return def;
}

std::string serialize(const SettingsData& d)
{
    std::ostringstream os;
    os << "{\n";
    os << "  \"version\": 1,\n";
    os << "  \"ui\": {\n";
    os << R"(    "default_theme": ")" << escape_json(d.default_theme) << "\",\n";
    os << R"(    "default_data_palette": ")" << escape_json(d.default_data_palette) << "\",\n";
    os << "    \"inspector_visible\": " << (d.inspector_visible ? "true" : "false") << ",\n";
    os << "    \"nav_rail_visible\": " << (d.nav_rail_visible ? "true" : "false") << ",\n";
    os << "    \"timeline_visible\": " << (d.timeline_visible ? "true" : "false") << "\n";
    os << "  }\n";
    os << "}\n";
    return os.str();
}

}   // namespace

// ─── SettingsStore ───────────────────────────────────────────────────────────

std::string SettingsStore::default_path()
{
    const char* home = std::getenv("HOME");
    if (!home || home[0] == '\0')
        home = ".";
    return std::string(home) + "/.config/spectra/settings.json";
}

bool SettingsStore::load()
{
    return load(default_path());
}

bool SettingsStore::load(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open())
        return false;
    std::string json(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>{});

    const std::string theme = read_json_string(json, "default_theme");
    if (!theme.empty())
        data_.default_theme = theme;

    const std::string palette = read_json_string(json, "default_data_palette");
    if (!palette.empty())
        data_.default_data_palette = palette;

    data_.inspector_visible = read_json_bool(json, "inspector_visible", data_.inspector_visible);
    data_.nav_rail_visible  = read_json_bool(json, "nav_rail_visible", data_.nav_rail_visible);
    data_.timeline_visible  = read_json_bool(json, "timeline_visible", data_.timeline_visible);

    loaded_path_ = path;
    return true;
}

bool SettingsStore::save() const
{
    const std::string& path = loaded_path_.empty() ? default_path() : loaded_path_;
    return save(path);
}

bool SettingsStore::save(const std::string& path) const
{
    std::filesystem::path p(path);
    if (p.has_parent_path())
    {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
    }
    std::ofstream f(path);
    if (!f.is_open())
        return false;
    f << serialize(data_);
    return f.good();
}

void SettingsStore::apply_to(ui::ThemeManager& tm) const
{
    if (!data_.default_theme.empty())
        tm.set_theme(data_.default_theme);
    if (!data_.default_data_palette.empty())
        tm.set_data_palette(data_.default_data_palette);
}

void SettingsStore::notify_change()
{
    if (on_change_)
        on_change_();
}

}   // namespace spectra::ui::settings
