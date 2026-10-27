// plugin_export_commands.cpp — File commands for plugin export formats.

#include "plugin_export_commands.hpp"

#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>
#include <spectra/series.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <span>

#include "ui/automation/automation_json.hpp"
#include "ui/commands/command_registry.hpp"
#include "ui/export_dialog.hpp"
#include "ui/theme/icons.hpp"

namespace spectra
{

namespace
{

constexpr std::string_view kCommandPrefix = "file.export_plugin.";

template <typename T>
void append_number(std::string& out, T value)
{
    if (!std::isfinite(value))
    {
        out += "null";
        return;
    }
    char buf[64];
    auto [end, ec] = std::to_chars(buf, buf + sizeof(buf), value);
    if (ec != std::errc{})
    {
        out += "null";
        return;
    }
    out.append(buf, end);
}

// Writes "[v0,v1,...]" with each value = offset + data[i].  A zero offset keeps
// the float's own shortest representation (0.1 rather than 0.10000000149...).
void append_array(std::string& out, std::span<const float> data, double offset)
{
    out += '[';
    for (size_t i = 0; i < data.size(); ++i)
    {
        if (i > 0)
            out += ',';
        if (offset == 0.0)
            append_number(out, data[i]);
        else
            append_number(out, offset + static_cast<double>(data[i]));
    }
    out += ']';
}

void append_string(std::string& out, std::string_view key, const std::string& value)
{
    out += '"';
    out += key;
    out += "\":\"";
    out += json_escape(value);
    out += '"';
}

void append_xy_series(std::string&           out,
                      const Series&          s,
                      const char*            type,
                      std::span<const float> x,
                      std::span<const float> y)
{
    out += '{';
    append_string(out, "label", s.label());
    out += ",\"type\":\"";
    out += type;
    out += "\",\"x\":";
    append_array(out, x, s.x_offset());
    out += ",\"y\":";
    append_array(out, y, s.y_offset());
    out += '}';
}

}   // namespace

std::string plugin_export_command_id(std::string_view format_name)
{
    std::string id(kCommandPrefix);
    id += format_name;
    return id;
}

std::string figure_to_export_json(const Figure& figure)
{
    std::string out        = "{\"axes\":[";
    bool        first_axes = true;
    size_t      index      = 0;
    for (const auto& ax : figure.axes())
    {
        if (!ax)
        {
            ++index;
            continue;
        }
        if (!first_axes)
            out += ',';
        first_axes = false;

        out += "{\"index\":";
        out += std::to_string(index++);
        out += ',';
        append_string(out, "title", ax->title());
        out += ',';
        append_string(out, "xlabel", ax->xlabel());
        out += ',';
        append_string(out, "ylabel", ax->ylabel());
        out += ",\"series\":[";

        bool first_series = true;
        for (const auto& s : ax->series())
        {
            if (!s)
                continue;
            const size_t before = out.size();
            if (!first_series)
                out += ',';
            if (auto* ls = dynamic_cast<const LineSeries*>(s.get()))
                append_xy_series(out, *ls, "line", ls->x_data(), ls->y_data());
            else if (auto* ss = dynamic_cast<const ScatterSeries*>(s.get()))
                append_xy_series(out, *ss, "scatter", ss->x_data(), ss->y_data());
            else
            {
                out.resize(before);   // no x/y data to export
                continue;
            }
            first_series = false;
        }
        out += "]}";
    }
    out += "]}";
    return out;
}

PluginExportCommands::~PluginExportCommands()
{
    detach();
}

void PluginExportCommands::attach(Options options)
{
    detach();
    if (!options.formats || !options.commands || !options.active_figure)
        return;
    options_ = std::move(options);

    listener_id_ = options_.formats->add_change_listener(
        [this](const ExportFormatInfo& info, ExportFormatRegistry::ChangeKind kind)
        {
            if (kind == ExportFormatRegistry::ChangeKind::Registered)
                add_command(info);
            else
                remove_command(info.name);
        });

    for (const auto& format : options_.formats->available_formats())
        add_command(format);
}

void PluginExportCommands::detach()
{
    if (options_.formats && listener_id_ != 0)
        options_.formats->remove_change_listener(listener_id_);
    listener_id_ = 0;

    if (options_.commands)
    {
        for (const auto& id : registered_ids_)
            options_.commands->unregister_command(id);
    }
    registered_ids_.clear();
    options_ = Options{};
}

void PluginExportCommands::add_command(const ExportFormatInfo& format)
{
    const std::string id = plugin_export_command_id(format.name);

    ExportFormatRegistry*    formats         = options_.formats;
    Figure**                 active_figure   = options_.active_figure;
    std::string*             last_export_dir = options_.last_export_dir;
    PluginExportPathProvider ask_path        = options_.ask_path;

    Command cmd;
    cmd.id       = id;
    cmd.label    = "Export " + format.name + " (." + format.extension + ")";
    cmd.category = "File";
    cmd.icon     = static_cast<uint16_t>(ui::Icon::Export);
    cmd.callback = [formats, active_figure, last_export_dir, ask_path, format]()
    {
        Figure* fig = *active_figure;
        if (!fig)
            return;

        std::optional<std::string> path;
        if (ask_path)
        {
            path = ask_path(format);
        }
        else
        {
            const std::string pattern      = "*." + format.extension;
            const std::string default_name = "spectra_export." + format.extension;
            const char*       patterns[]   = {pattern.c_str()};
            std::string       fallback_dir;
            path = ask_export_path(("Export " + format.name).c_str(),
                                   default_name.c_str(),
                                   1,
                                   patterns,
                                   format.name.c_str(),
                                   last_export_dir ? *last_export_dir : fallback_dir);
        }
        if (!path || path->empty())
            return;

        const std::string json = figure_to_export_json(*fig);
        if (!formats->export_figure(format.name, json, nullptr, 0, 0, *path))
        {
            SPECTRA_LOG_WARN("export", "Plugin export '{}' to '{}' failed", format.name, *path);
        }
    };
    options_.commands->register_command(std::move(cmd));

    if (std::find(registered_ids_.begin(), registered_ids_.end(), id) == registered_ids_.end())
        registered_ids_.push_back(id);
}

void PluginExportCommands::remove_command(const std::string& format_name)
{
    const std::string id = plugin_export_command_id(format_name);
    auto              it = std::find(registered_ids_.begin(), registered_ids_.end(), id);
    if (it == registered_ids_.end())
        return;
    registered_ids_.erase(it);
    options_.commands->unregister_command(id);
}

}   // namespace spectra
