#include "plugin_api.hpp"

#include <cstdlib>
#include <filesystem>

#include "command_registry.hpp"
#include "shortcut_manager.hpp"
#include "undo_manager.hpp"

#ifdef _WIN32
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

namespace spectra
{

// ─── C ABI host functions ────────────────────────────────────────────────────

extern "C"
{
    int plotix_register_command(PlotixCommandRegistry registry, const PlotixCommandDesc* desc)
    {
        if (!registry || !desc || !desc->id || !desc->label)
            return -1;

        auto* reg = static_cast<CommandRegistry*>(registry);
        Command cmd;
        cmd.id = desc->id;
        cmd.label = desc->label;
        cmd.category = desc->category ? desc->category : "Plugin";
        cmd.shortcut = desc->shortcut_hint ? desc->shortcut_hint : "";

        // Wrap the C callback in a std::function, capturing user_data
        auto cb = desc->callback;
        auto ud = desc->user_data;
        if (cb)
        {
            cmd.callback = [cb, ud]() { cb(ud); };
        }

        reg->register_command(std::move(cmd));
        return 0;
    }

    int plotix_unregister_command(PlotixCommandRegistry registry, const char* command_id)
    {
        if (!registry || !command_id)
            return -1;
        auto* reg = static_cast<CommandRegistry*>(registry);
        reg->unregister_command(command_id);
        return 0;
    }

    int plotix_execute_command(PlotixCommandRegistry registry, const char* command_id)
    {
        if (!registry || !command_id)
            return -1;
        auto* reg = static_cast<CommandRegistry*>(registry);
        return reg->execute(command_id) ? 0 : -1;
    }

    int plotix_bind_shortcut(PlotixShortcutManager manager,
                             const char* shortcut_str,
                             const char* command_id)
    {
        if (!manager || !shortcut_str || !command_id)
            return -1;
        auto* mgr = static_cast<ShortcutManager*>(manager);
        Shortcut sc = Shortcut::from_string(shortcut_str);
        if (!sc.valid())
            return -1;
        mgr->bind(sc, command_id);
        return 0;
    }

    int plotix_push_undo(PlotixUndoManager manager,
                         const char* description,
                         PlotixCommandCallback undo_fn,
                         void* undo_data,
                         PlotixCommandCallback redo_fn,
                         void* redo_data)
    {
        if (!manager || !description)
            return -1;
        auto* undo = static_cast<UndoManager*>(manager);

        UndoAction action;
        action.description = description;
        if (undo_fn)
        {
            action.undo_fn = [undo_fn, undo_data]() { undo_fn(undo_data); };
        }
        if (redo_fn)
        {
            action.redo_fn = [redo_fn, redo_data]() { redo_fn(redo_data); };
        }
        undo->push(std::move(action));
        return 0;
    }

}  // extern "C"

// ─── PluginManager ───────────────────────────────────────────────────────────

PluginManager::~PluginManager()
{
    unload_all();
}

PlotixPluginContext PluginManager::make_context() const
{
    PlotixPluginContext ctx{};
    ctx.api_version_major = PLOTIX_PLUGIN_API_VERSION_MAJOR;
    ctx.api_version_minor = PLOTIX_PLUGIN_API_VERSION_MINOR;
    ctx.command_registry = static_cast<PlotixCommandRegistry>(registry_);
    ctx.shortcut_manager = static_cast<PlotixShortcutManager>(shortcut_mgr_);
    ctx.undo_manager = static_cast<PlotixUndoManager>(undo_mgr_);
    return ctx;
}

bool PluginManager::load_plugin(const std::string& path)
{
    std::lock_guard lock(mutex_);

    // Check if already loaded
    for (const auto& p : plugins_)
    {
        if (p.path == path && p.loaded)
            return false;
    }

    void* handle = nullptr;
    PlotixPluginInitFn init_fn = nullptr;
    PlotixPluginShutdownFn shutdown_fn = nullptr;

#ifdef _WIN32
    handle = LoadLibraryA(path.c_str());
    if (!handle)
        return false;
    init_fn = reinterpret_cast<PlotixPluginInitFn>(
        GetProcAddress(static_cast<HMODULE>(handle), "plotix_plugin_init"));
    shutdown_fn = reinterpret_cast<PlotixPluginShutdownFn>(
        GetProcAddress(static_cast<HMODULE>(handle), "plotix_plugin_shutdown"));
#else
    handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle)
        return false;
    init_fn = reinterpret_cast<PlotixPluginInitFn>(dlsym(handle, "plotix_plugin_init"));
    shutdown_fn = reinterpret_cast<PlotixPluginShutdownFn>(dlsym(handle, "plotix_plugin_shutdown"));
#endif

    if (!init_fn)
    {
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(handle));
#else
        dlclose(handle);
#endif
        return false;
    }

    PlotixPluginContext ctx = make_context();
    PlotixPluginInfo info{};
    int result = init_fn(&ctx, &info);
    if (result != 0)
    {
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(handle));
#else
        dlclose(handle);
#endif
        return false;
    }

    // Version compatibility check
    if (info.api_version_major != PLOTIX_PLUGIN_API_VERSION_MAJOR)
    {
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(handle));
#else
        dlclose(handle);
#endif
        return false;
    }

    PluginEntry entry;
    entry.name = info.name ? info.name : "Unknown";
    entry.version = info.version ? info.version : "0.0.0";
    entry.author = info.author ? info.author : "";
    entry.description = info.description ? info.description : "";
    entry.path = path;
    entry.loaded = true;
    entry.enabled = true;
    entry.handle = handle;
    entry.shutdown_fn = shutdown_fn;

    plugins_.push_back(std::move(entry));
    return true;
}

bool PluginManager::unload_plugin(const std::string& name)
{
    std::lock_guard lock(mutex_);

    for (auto it = plugins_.begin(); it != plugins_.end(); ++it)
    {
        if (it->name == name && it->loaded)
        {
            // Unregister commands
            if (registry_)
            {
                for (const auto& cmd_id : it->registered_commands)
                {
                    registry_->unregister_command(cmd_id);
                }
            }

            // Call shutdown
            if (it->shutdown_fn)
            {
                it->shutdown_fn();
            }

            // Close library
            if (it->handle)
            {
#ifdef _WIN32
                FreeLibrary(static_cast<HMODULE>(it->handle));
#else
                dlclose(it->handle);
#endif
            }

            plugins_.erase(it);
            return true;
        }
    }
    return false;
}

void PluginManager::unload_all()
{
    std::lock_guard lock(mutex_);

    for (auto& p : plugins_)
    {
        if (!p.loaded)
            continue;

        if (registry_)
        {
            for (const auto& cmd_id : p.registered_commands)
            {
                registry_->unregister_command(cmd_id);
            }
        }

        if (p.shutdown_fn)
        {
            p.shutdown_fn();
        }

        if (p.handle)
        {
#ifdef _WIN32
            FreeLibrary(static_cast<HMODULE>(p.handle));
#else
            dlclose(p.handle);
#endif
        }
        p.loaded = false;
    }
    plugins_.clear();
}

std::vector<PluginEntry> PluginManager::plugins() const
{
    std::lock_guard lock(mutex_);
    return plugins_;
}

const PluginEntry* PluginManager::find_plugin(const std::string& name) const
{
    std::lock_guard lock(mutex_);
    for (const auto& p : plugins_)
    {
        if (p.name == name)
            return &p;
    }
    return nullptr;
}

size_t PluginManager::plugin_count() const
{
    std::lock_guard lock(mutex_);
    return plugins_.size();
}

void PluginManager::set_plugin_enabled(const std::string& name, bool enabled)
{
    std::lock_guard lock(mutex_);
    for (auto& p : plugins_)
    {
        if (p.name == name)
        {
            p.enabled = enabled;
            // Enable/disable all commands registered by this plugin
            if (registry_)
            {
                for (const auto& cmd_id : p.registered_commands)
                {
                    registry_->set_enabled(cmd_id, enabled);
                }
            }
            break;
        }
    }
}

std::vector<std::string> PluginManager::discover(const std::string& directory) const
{
    std::vector<std::string> paths;
    try
    {
        if (!std::filesystem::exists(directory))
            return paths;
        for (const auto& entry : std::filesystem::directory_iterator(directory))
        {
            if (!entry.is_regular_file())
                continue;
            auto ext = entry.path().extension().string();
#ifdef _WIN32
            if (ext == ".dll")
            {
#elif defined(__APPLE__)
            if (ext == ".dylib")
            {
#else
            if (ext == ".so")
            {
#endif
                paths.push_back(entry.path().string());
            }
        }
    }
    catch (...)
    {
        // Ignore filesystem errors
    }
    return paths;
}

std::string PluginManager::default_plugin_dir()
{
    const char* home = std::getenv("HOME");
    if (!home)
        home = std::getenv("USERPROFILE");
    if (!home)
        return "plugins";

    std::filesystem::path dir = std::filesystem::path(home) / ".config" / "spectra" / "plugins";
    return dir.string();
}

// ─── Serialization ───────────────────────────────────────────────────────────

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
            default:
                out += c;
                break;
        }
    }
    return out;
}

std::string PluginManager::serialize_state() const
{
    std::lock_guard lock(mutex_);
    std::string out = "{\n  \"plugins\": [\n";
    for (size_t i = 0; i < plugins_.size(); ++i)
    {
        const auto& p = plugins_[i];
        out += "    {\"name\": \"" + escape_json(p.name) + "\", \"path\": \"" + escape_json(p.path)
               + "\", \"enabled\": " + (p.enabled ? "true" : "false") + "}";
        if (i + 1 < plugins_.size())
            out += ",";
        out += "\n";
    }
    out += "  ]\n}\n";
    return out;
}

bool PluginManager::deserialize_state(const std::string& json)
{
    // Minimal parse: find plugin names and enabled states
    // This is used to restore enabled/disabled state after loading plugins
    std::lock_guard lock(mutex_);

    size_t pos = 0;
    while ((pos = json.find("\"name\"", pos)) != std::string::npos)
    {
        pos += 6;
        auto q1 = json.find('"', json.find(':', pos) + 1);
        if (q1 == std::string::npos)
            break;
        auto q2 = json.find('"', q1 + 1);
        if (q2 == std::string::npos)
            break;
        std::string name = json.substr(q1 + 1, q2 - q1 - 1);

        // Find enabled field nearby
        auto enabled_pos = json.find("\"enabled\"", q2);
        if (enabled_pos != std::string::npos && enabled_pos < q2 + 200)
        {
            auto colon = json.find(':', enabled_pos);
            if (colon != std::string::npos)
            {
                auto rest = json.substr(colon + 1, 10);
                bool enabled = rest.find("true") != std::string::npos;

                for (auto& p : plugins_)
                {
                    if (p.name == name)
                    {
                        p.enabled = enabled;
                        break;
                    }
                }
            }
        }
        pos = q2 + 1;
    }
    return true;
}

}  // namespace spectra
