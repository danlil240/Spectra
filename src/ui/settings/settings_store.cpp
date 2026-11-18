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
    os << R"(    "last_dark_theme": ")" << escape_json(d.last_dark_theme) << "\",\n";
    os << R"(    "last_light_theme": ")" << escape_json(d.last_light_theme) << "\",\n";
    os << "    \"inspector_visible\": " << (d.inspector_visible ? "true" : "false") << ",\n";
    os << "    \"nav_rail_visible\": " << (d.nav_rail_visible ? "true" : "false") << ",\n";
    os << "    \"timeline_visible\": " << (d.timeline_visible ? "true" : "false") << ",\n";
    os << "    \"ui_scale\": " << d.ui_scale << ",\n";
    os << "    \"transform_overwrite\": " << (d.transform_overwrite ? "true" : "false") << "\n";
    os << "  },\n";
    // Keys are unique across the file (the reader looks them up by name).
    const ExportDialogSettings& e = d.export_dialog;
    os << "  \"export\": {\n";
    os << R"(    "export_format": ")" << export_format_extension(e.format) << "\",\n";
    os << R"(    "export_theme": ")" << escape_json(e.theme) << "\",\n";
    os << R"(    "export_style": ")" << escape_json(e.style) << "\",\n";
    os << R"(    "export_unit": ")" << (e.unit == ExportSizeUnit::Inches ? "in" : "px") << "\",\n";
    os << "    \"export_width_px\": " << e.width_px << ",\n";
    os << "    \"export_height_px\": " << e.height_px << ",\n";
    os << "    \"export_width_in\": " << e.width_in << ",\n";
    os << "    \"export_height_in\": " << e.height_in << ",\n";
    os << "    \"export_dpi\": " << e.dpi << ",\n";
    os << "    \"export_scale\": " << e.scale << ",\n";
    os << "    \"export_transparent\": " << (e.transparent ? "true" : "false") << "\n";
    os << "  },\n";
    // Glass / transparency (Settings -> Appearance).  "glass_custom" is always
    // written so a reader can tell "defaults" from "file predates glass".
    os << "  \"glass\": {\n";
    os << "    \"glass_custom\": " << (d.glass ? "true" : "false");
    if (d.glass)
    {
        const ui::ThemeGlassSettings& g = *d.glass;
        os << ",\n";
        os << R"(    "glass_theme": ")" << escape_json(d.glass_theme) << "\",\n";
        os << "    \"glass_master_intensity\": " << g.master_intensity << ",\n";
        os << "    \"glass_panel_alpha\": " << g.panel_alpha << ",\n";
        os << "    \"glass_plot_alpha\": " << g.plot_alpha << ",\n";
        os << "    \"glass_toolbar_alpha\": " << g.toolbar_alpha << ",\n";
        os << "    \"glass_glow_strength\": " << g.glow_strength << ",\n";
        os << "    \"glass_blur_enabled\": " << (g.blur_enabled ? "true" : "false");
    }
    os << "\n  },\n";
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

    const std::string last_dark = read_json_string(json, "last_dark_theme");
    if (!last_dark.empty())
        data_.last_dark_theme = last_dark;
    const std::string last_light = read_json_string(json, "last_light_theme");
    if (!last_light.empty())
        data_.last_light_theme = last_light;

    data_.inspector_visible = read_json_bool(json, "inspector_visible", data_.inspector_visible);
    data_.nav_rail_visible  = read_json_bool(json, "nav_rail_visible", data_.nav_rail_visible);
    data_.timeline_visible  = read_json_bool(json, "timeline_visible", data_.timeline_visible);
    data_.ui_scale          = ui::ThemeManager::clamp_ui_scale(
        static_cast<float>(read_json_number(json, "ui_scale", data_.ui_scale)));
    data_.transform_overwrite =
        read_json_bool(json, "transform_overwrite", data_.transform_overwrite);

    {
        ExportDialogSettings& e   = data_.export_dialog;
        const std::string     fmt = read_json_string(json, "export_format");
        if (!fmt.empty())
            e.format = export_format_from_string(fmt);
        const std::string theme = read_json_string(json, "export_theme");
        if (!theme.empty())
            e.theme = theme;
        e.style       = read_json_string(json, "export_style");
        e.unit        = read_json_string(json, "export_unit") == "in" ? ExportSizeUnit::Inches
                                                                      : ExportSizeUnit::Pixels;
        auto read_u32 = [&](const char* key, uint32_t def)
        {
            const double v = read_json_number(json, key, def);
            return (v >= 0.0 && v <= MAX_EXPORT_PIXELS) ? static_cast<uint32_t>(v) : def;
        };
        e.width_px    = read_u32("export_width_px", e.width_px);
        e.height_px   = read_u32("export_height_px", e.height_px);
        e.width_in    = static_cast<float>(read_json_number(json, "export_width_in", e.width_in));
        e.height_in   = static_cast<float>(read_json_number(json, "export_height_in", e.height_in));
        e.dpi         = static_cast<float>(read_json_number(json, "export_dpi", e.dpi));
        e.scale       = static_cast<float>(read_json_number(json, "export_scale", e.scale));
        e.transparent = read_json_bool(json, "export_transparent", e.transparent);
        e             = sanitized(e);
    }
    has_saved_glass_ = json_has_key_token(json, "glass_custom");
    data_.glass.reset();
    data_.glass_theme.clear();
    if (read_json_bool(json, "glass_custom", false))
    {
        const std::string glass_theme = read_json_string(json, "glass_theme");
        if (!glass_theme.empty())
        {
            ui::ThemeGlassSettings g;
            auto                   read_f = [&](const char* key, float def)
            { return static_cast<float>(read_json_number(json, key, def)); };
            g.master_intensity = read_f("glass_master_intensity", g.master_intensity);
            g.panel_alpha      = read_f("glass_panel_alpha", g.panel_alpha);
            g.plot_alpha       = read_f("glass_plot_alpha", g.plot_alpha);
            g.toolbar_alpha    = read_f("glass_toolbar_alpha", g.toolbar_alpha);
            g.glow_strength    = read_f("glass_glow_strength", g.glow_strength);
            g.blur_enabled     = read_json_bool(json, "glass_blur_enabled", g.blur_enabled);
            g.clamp();
            data_.glass       = g;
            data_.glass_theme = glass_theme;
        }
    }

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
    apply_startup_appearance(tm, &data_);
}

bool SettingsStore::migrate_legacy_glass(const ui::ThemeManager& tm)
{
    if (has_saved_glass_)
        return false;
    std::string            theme;
    ui::ThemeGlassSettings g;
    if (!tm.read_default_glass(theme, g))
        return false;
    // Mark the file as migrated even when nothing differs, so a later
    // "Reset Theme Defaults" is not undone by the old file at the next start.
    has_saved_glass_       = true;
    const ui::Theme* found = tm.find_theme(theme);
    if (!found || found->glass_defaults == g)
    {
        notify_change();
        return false;
    }
    data_.glass       = g;
    data_.glass_theme = theme;
    notify_change();
    return true;
}

void apply_startup_appearance(ui::ThemeManager&   tm,
                              const SettingsData* user,
                              const std::string&  theme,
                              const std::string&  data_palette)
{
    // A freshly constructed ThemeManager has no themes registered yet, so
    // set_theme()/set_data_palette() would silently no-op and the first
    // ThemeManager::instance() call would then fall back to "night".
    tm.ensure_initialized();
    // Before set_theme(), which records the startup theme as the last of its kind.
    if (user)
        tm.set_last_themes(user->last_dark_theme, user->last_light_theme);
    const StartupTheme startup = resolve_startup_theme(user, theme, data_palette);
    tm.set_theme(startup.theme);
    tm.set_data_palette(startup.data_palette);
    if (!user)
        return;
    tm.set_ui_scale(user->ui_scale);
    // Glass belongs to the theme it was set for: another startup theme (an
    // explicit override, or a theme made default later) gets its own defaults,
    // exactly as picking that theme in Settings does.
    if (user->glass && user->glass_theme == tm.current_theme_name())
        tm.set_glass_settings(*user->glass, true);
    else
        tm.reset_glass_defaults();
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
    store.data_mut().default_theme    = theme;
    store.data_mut().last_dark_theme  = tm.last_dark_theme();
    store.data_mut().last_light_theme = tm.last_light_theme();
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
    // Start with the glass the user sees now, not glass saved for another theme.
    if (!remember_glass(tm, store))
        store.notify_change();
}

bool remember_glass(const ui::ThemeManager& tm, SettingsStore& store)
{
    SettingsData&                         d       = store.data_mut();
    const ui::Theme*                      current = tm.find_theme(tm.current_theme_name());
    std::optional<ui::ThemeGlassSettings> glass;
    std::string                           glass_theme;
    if (!current || !(current->glass_defaults == tm.glass()))
    {
        glass       = tm.glass();
        glass_theme = tm.current_theme_name();
    }
    if (d.glass == glass && d.glass_theme == glass_theme)
        return false;
    d.glass       = glass;
    d.glass_theme = glass_theme;
    store.notify_change();
    return true;
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

bool remember_toggle_themes(const ui::ThemeManager& tm, SettingsStore& store)
{
    SettingsData& d = store.data_mut();
    if (d.last_dark_theme == tm.last_dark_theme() && d.last_light_theme == tm.last_light_theme())
        return false;
    d.last_dark_theme  = tm.last_dark_theme();
    d.last_light_theme = tm.last_light_theme();
    store.notify_change();
    return true;
}

void SettingsStore::notify_change()
{
    if (on_change_)
        on_change_();
}

}   // namespace spectra::ui::settings
