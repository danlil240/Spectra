#include "shortcut_config.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "shortcut_manager.hpp"

namespace plotix
{

// ─── Override management ─────────────────────────────────────────────────────

void ShortcutConfig::set_override(const std::string& command_id, const std::string& shortcut_str)
{
    // Update existing or add new
    for (auto& o : overrides_)
    {
        if (o.command_id == command_id)
        {
            o.shortcut_str = shortcut_str;
            o.removed = shortcut_str.empty();
            notify_change();
            return;
        }
    }
    BindingOverride bo;
    bo.command_id = command_id;
    bo.shortcut_str = shortcut_str;
    bo.removed = shortcut_str.empty();
    overrides_.push_back(std::move(bo));
    notify_change();
}

void ShortcutConfig::remove_override(const std::string& command_id)
{
    std::erase_if(overrides_, [&](const BindingOverride& o) { return o.command_id == command_id; });
    notify_change();
}

bool ShortcutConfig::has_override(const std::string& command_id) const
{
    for (const auto& o : overrides_)
    {
        if (o.command_id == command_id)
            return true;
    }
    return false;
}

std::vector<ShortcutConfig::BindingOverride> ShortcutConfig::overrides() const
{
    return overrides_;
}

size_t ShortcutConfig::override_count() const
{
    return overrides_.size();
}

void ShortcutConfig::reset_all()
{
    overrides_.clear();
    notify_change();
}

void ShortcutConfig::apply_overrides()
{
    if (!manager_)
        return;

    for (const auto& o : overrides_)
    {
        if (o.removed || o.shortcut_str.empty())
        {
            // Unbind the command's current shortcut
            manager_->unbind_command(o.command_id);
        }
        else
        {
            // Unbind old shortcut for this command first
            manager_->unbind_command(o.command_id);
            // Bind the new shortcut
            Shortcut sc = Shortcut::from_string(o.shortcut_str);
            if (sc.valid())
            {
                manager_->bind(sc, o.command_id);
            }
        }
    }
}

// ─── JSON serialization ──────────────────────────────────────────────────────

static std::string escape_json(const std::string& s)
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

std::string ShortcutConfig::serialize() const
{
    std::ostringstream os;
    os << "{\n";
    os << "  \"version\": 1,\n";
    os << "  \"bindings\": [\n";
    for (size_t i = 0; i < overrides_.size(); ++i)
    {
        const auto& o = overrides_[i];
        os << "    {\n";
        os << "      \"command\": \"" << escape_json(o.command_id) << "\",\n";
        os << "      \"shortcut\": \"" << escape_json(o.shortcut_str) << "\",\n";
        os << "      \"removed\": " << (o.removed ? "true" : "false") << "\n";
        os << "    }";
        if (i + 1 < overrides_.size())
            os << ",";
        os << "\n";
    }
    os << "  ]\n";
    os << "}\n";
    return os.str();
}

// Minimal JSON parser for our specific format
static std::string read_json_string(const std::string& json, const std::string& key)
{
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos)
        return "";
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos)
        return "";
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos)
        return "";
    size_t end = pos + 1;
    while (end < json.size())
    {
        if (json[end] == '"' && json[end - 1] != '\\')
            break;
        ++end;
    }
    return json.substr(pos + 1, end - pos - 1);
}

static bool read_json_bool(const std::string& json, const std::string& key, bool def)
{
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos)
        return def;
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos)
        return def;
    auto rest = json.substr(pos + 1, 10);
    size_t start = rest.find_first_not_of(" \t\n\r");
    if (start == std::string::npos)
        return def;
    if (rest.substr(start, 4) == "true")
        return true;
    if (rest.substr(start, 5) == "false")
        return false;
    return def;
}

static std::vector<std::string> parse_binding_objects(const std::string& json)
{
    std::vector<std::string> objects;
    std::string search = "\"bindings\"";
    auto pos = json.find(search);
    if (pos == std::string::npos)
        return objects;
    pos = json.find('[', pos);
    if (pos == std::string::npos)
        return objects;

    int depth = 0;
    size_t obj_start = 0;
    for (size_t i = pos + 1; i < json.size(); ++i)
    {
        if (json[i] == '{')
        {
            if (depth == 0)
                obj_start = i;
            ++depth;
        }
        else if (json[i] == '}')
        {
            --depth;
            if (depth == 0)
            {
                objects.push_back(json.substr(obj_start, i - obj_start + 1));
            }
        }
        else if (json[i] == ']' && depth == 0)
        {
            break;
        }
    }
    return objects;
}

bool ShortcutConfig::deserialize(const std::string& json)
{
    if (json.empty())
        return false;

    // Check version
    std::string search = "\"version\"";
    auto vpos = json.find(search);
    if (vpos != std::string::npos)
    {
        auto cpos = json.find(':', vpos + search.size());
        if (cpos != std::string::npos)
        {
            int ver = std::atoi(json.c_str() + cpos + 1);
            if (ver > 1)
                return false;  // Future version
        }
    }

    overrides_.clear();
    auto objects = parse_binding_objects(json);
    for (const auto& obj : objects)
    {
        BindingOverride bo;
        bo.command_id = read_json_string(obj, "command");
        bo.shortcut_str = read_json_string(obj, "shortcut");
        bo.removed = read_json_bool(obj, "removed", false);
        if (!bo.command_id.empty())
        {
            overrides_.push_back(std::move(bo));
        }
    }
    return true;
}

// ─── File I/O ────────────────────────────────────────────────────────────────

bool ShortcutConfig::save(const std::string& path) const
{
    // Create parent directories
    try
    {
        auto dir = std::filesystem::path(path).parent_path();
        if (!dir.empty())
        {
            std::filesystem::create_directories(dir);
        }
    }
    catch (...)
    {
        // Ignore directory creation errors
    }

    std::ofstream f(path);
    if (!f.is_open())
        return false;
    f << serialize();
    return f.good();
}

bool ShortcutConfig::load(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open())
        return false;
    std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return deserialize(json);
}

std::string ShortcutConfig::default_path()
{
    const char* home = std::getenv("HOME");
    if (!home)
        home = std::getenv("USERPROFILE");
    if (!home)
        return "keybindings.json";

    std::filesystem::path dir = std::filesystem::path(home) / ".config" / "plotix";
    return (dir / "keybindings.json").string();
}

void ShortcutConfig::notify_change()
{
    if (on_change_)
        on_change_();
}

}  // namespace plotix
