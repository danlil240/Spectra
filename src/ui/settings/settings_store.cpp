#include "settings_store.hpp"
#include "core/config_paths.hpp"
#include "core/json_key.hpp"

#include <algorithm>
#include <cmath>
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
    auto        pos    = json_find_key(json, key);
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
    auto        pos    = json_find_key(json, key);
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

// Reads a JSON array of strings ("key": ["a", "b"]), unescaping \\ \" \n \r \t.
std::vector<std::string> read_json_string_array(const std::string& json, const std::string& key)
{
    std::vector<std::string> out;
    auto                     pos = json_find_key(json, key);
    if (pos == std::string::npos)
        return out;
    pos = json.find(':', pos + key.size() + 2);
    if (pos == std::string::npos)
        return out;
    pos = json.find_first_not_of(" \t\r\n", pos + 1);
    if (pos == std::string::npos || json[pos] != '[')
        return out;
    ++pos;
    while (pos < json.size())
    {
        pos = json.find_first_not_of(" \t\r\n,", pos);
        if (pos == std::string::npos || json[pos] == ']')
            break;
        if (json[pos] != '"')
            break;   // not a string array
        std::string value;
        ++pos;
        while (pos < json.size() && json[pos] != '"')
        {
            char c = json[pos];
            if (c == '\\' && pos + 1 < json.size())
            {
                const char e = json[++pos];
                c            = e == 'n' ? '\n' : e == 'r' ? '\r' : e == 't' ? '\t' : e;
            }
            value += c;
            ++pos;
        }
        ++pos;   // closing quote
        out.push_back(std::move(value));
    }
    return out;
}

double read_json_number(const std::string& json, const std::string& key, double def)
{
    auto pos = json_find_key(json, key);
    if (pos == std::string::npos)
        return def;
    pos = json.find(':', pos + key.size() + 2);
    if (pos == std::string::npos)
        return def;
    const char*  begin = json.c_str() + pos + 1;
    char*        end   = nullptr;
    const double v     = std::strtod(begin, &end);
    return (end == begin || !std::isfinite(v)) ? def : v;
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
    os << "    \"timeline_visible\": " << (d.timeline_visible ? "true" : "false") << ",\n";
    os << "    \"ui_scale\": " << d.ui_scale << "\n";
    os << "  },\n";
    os << "  \"recent_files\": [";
    for (size_t i = 0; i < d.recent_files.size(); ++i)
        os << (i ? ", " : "") << '"' << escape_json(d.recent_files[i]) << '"';
    os << "]\n";
    os << "}\n";
    return os.str();
}

}   // namespace

StartupTheme resolve_startup_theme(const SettingsData* user,
                                   const std::string&  theme,
                                   const std::string&  data_palette)
{
    const SettingsData  defaults;
    const SettingsData& base = user ? *user : defaults;

    StartupTheme out;
    out.theme        = !theme.empty() ? theme : base.default_theme;
    out.data_palette = !data_palette.empty() ? data_palette : base.default_data_palette;
    if (out.theme.empty())
        out.theme = defaults.default_theme;
    if (out.data_palette.empty())
        out.data_palette = defaults.default_data_palette;
    return out;
}

// ─── SettingsStore ───────────────────────────────────────────────────────────

std::string SettingsStore::default_path()
{
    std::filesystem::path dir = user_config_dir();
    if (dir.empty())
        dir = std::filesystem::path(".") / ".config" / "spectra";
    return (dir / "settings.json").string();
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
    data_.ui_scale          = ui::ThemeManager::clamp_ui_scale(
        static_cast<float>(read_json_number(json, "ui_scale", data_.ui_scale)));

    data_.recent_files.clear();
    for (const std::string& f : read_json_string_array(json, "recent_files"))
    {
        if (!f.empty()
            && std::find(data_.recent_files.begin(), data_.recent_files.end(), f)
                   == data_.recent_files.end()
            && data_.recent_files.size() < MAX_RECENT_FILES)
            data_.recent_files.push_back(f);
    }

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
    // A freshly constructed ThemeManager has no themes registered yet, so
    // set_theme()/set_data_palette() would silently no-op and the first
    // ThemeManager::instance() call would then fall back to "night".
    tm.ensure_initialized();
    if (!data_.default_theme.empty())
        tm.set_theme(data_.default_theme);
    if (!data_.default_data_palette.empty())
        tm.set_data_palette(data_.default_data_palette);
    tm.set_ui_scale(data_.ui_scale);
}

AppearanceState appearance_state(const ui::ThemeManager& tm, const SettingsData& d)
{
    return AppearanceState{tm.current_theme_name(),
                           d.default_theme,
                           tm.current_data_palette_name(),
                           d.default_data_palette};
}

bool select_theme(ui::ThemeManager& tm, SettingsStore& store, const std::string& theme)
{
    tm.set_theme(theme);
    if (tm.current_theme_name() != theme)
        return false;
    store.data_mut().default_theme = theme;
    store.notify_change();
    return true;
}

bool select_data_palette(ui::ThemeManager& tm, SettingsStore& store, const std::string& palette)
{
    tm.set_data_palette(palette);
    if (tm.current_data_palette_name() != palette)
        return false;
    store.data_mut().default_data_palette = palette;
    store.notify_change();
    return true;
}

void make_active_theme_default(const ui::ThemeManager& tm, SettingsStore& store)
{
    store.data_mut().default_theme = tm.current_theme_name();
    store.notify_change();
}

void make_active_data_palette_default(const ui::ThemeManager& tm, SettingsStore& store)
{
    store.data_mut().default_data_palette = tm.current_data_palette_name();
    store.notify_change();
}

namespace
{
std::string normalize_recent_file(const std::string& path)
{
    std::error_code ec;
    auto            abs = std::filesystem::absolute(path, ec);
    return ec ? path : abs.lexically_normal().string();
}
}   // namespace

void push_recent_file(std::vector<std::string>& list, const std::string& path, size_t max_entries)
{
    if (path.empty())
        return;
    const std::string norm = normalize_recent_file(path);
    list.erase(std::remove(list.begin(), list.end(), norm), list.end());
    list.insert(list.begin(), norm);
    if (list.size() > std::max<size_t>(1, max_entries))
        list.resize(std::max<size_t>(1, max_entries));
}

void remove_recent_file(std::vector<std::string>& list, const std::string& path)
{
    const std::string norm = normalize_recent_file(path);
    list.erase(std::remove_if(list.begin(),
                              list.end(),
                              [&](const std::string& e) { return e == path || e == norm; }),
               list.end());
}

void SettingsStore::add_recent_file(const std::string& path)
{
    push_recent_file(data_.recent_files, path);
    notify_change();
}

void SettingsStore::forget_recent_file(const std::string& path)
{
    remove_recent_file(data_.recent_files, path);
    notify_change();
}

void SettingsStore::clear_recent_files()
{
    data_.recent_files.clear();
    notify_change();
}

void SettingsStore::notify_change()
{
    if (on_change_)
        on_change_();
}

}   // namespace spectra::ui::settings
