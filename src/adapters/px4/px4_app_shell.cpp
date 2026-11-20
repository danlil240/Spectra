#include "px4_app_shell.hpp"

#include <spectra/axes.hpp>
#include <spectra/chunked_series.hpp>
#include <spectra/figure.hpp>
#include <spectra/logger.hpp>
#include <spectra/series.hpp>

#include "ui/window/window_manager.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
    #include "../../../third_party/tinyfiledialogs.h"
    #include "../../ui/dialog_env_guard.hpp"
    #include "../../ui/native_dialog_policy.hpp"
    #include "ui/layout/layout_manager.hpp"
    #include "ui/overlay/canvas_overlay_layer.hpp"
    #include "ui/theme/theme.hpp"
    #include "ui/shell/app_shell.hpp"
#endif

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>

namespace spectra::adapters::px4
{

namespace
{

#ifdef SPECTRA_USE_IMGUI
std::string choose_ulog_file_path()
{
    if (!spectra::native_dialogs_enabled())
        return {};

    DialogEnvGuard env_guard;
    char const*    filter_patterns[] = {"*.ulg", "*.ulog"};
    const char*    home_env          = std::getenv("HOME");
    std::string    default_path      = home_env ? std::string(home_env) + "/" : "/";
    const char*    result            = tinyfd_openFileDialog("Open ULog",
                                               default_path.c_str(),
                                               2,
                                               filter_patterns,
                                               "PX4 ULog (*.ulg)",
                                               0);

    return result ? std::string(result) : std::string();
}
#endif

// ---------------------------------------------------------------------------
// Predefined auto-plot groups — flight-review style.
// Each entry: { group_title, ylabel, [ {topic, field, array_idx}, ... ] }
// Fields are tried in order; only those present in the ULog are added.
// ---------------------------------------------------------------------------

struct FieldDef
{
    const char* topic;
    const char* field;
    int         array_idx{-1};
    uint8_t     multi_id{0};
    const char* label_override{nullptr};
};

struct GroupDef
{
    const char*           title;
    const char*           ylabel;
    std::vector<FieldDef> fields;
};

// Minimum plot-area height per auto-plot row (see sync_auto_plot_figure).
constexpr float kAutoPlotMinSubplotHeight = 80.0f;

static const GroupDef k_auto_plot_groups[] = {
    {"Attitude (deg)",
     "deg",
     {
         {"vehicle_attitude_setpoint", "roll_body", -1, 0, "roll sp"},
         {"vehicle_attitude_setpoint", "pitch_body", -1, 0, "pitch sp"},
         {"vehicle_attitude_setpoint", "yaw_body", -1, 0, "yaw sp"},
     }},
    {"Angular Velocity (rad/s)",
     "rad/s",
     {
         {"vehicle_angular_velocity", "xyz", 0, 0, "roll rate"},
         {"vehicle_angular_velocity", "xyz", 1, 0, "pitch rate"},
         {"vehicle_angular_velocity", "xyz", 2, 0, "yaw rate"},
         {"vehicle_rates_setpoint", "roll", -1, 0, "roll rate sp"},
         {"vehicle_rates_setpoint", "pitch", -1, 0, "pitch rate sp"},
         {"vehicle_rates_setpoint", "yaw", -1, 0, "yaw rate sp"},
     }},
    {"Altitude (m)",
     "m",
     {
         {"vehicle_local_position", "z", -1, 0, "altitude (NED z)"},
         {"vehicle_local_position_setpoint", "z", -1, 0, "altitude sp"},
     }},
    {"Velocity (m/s)",
     "m/s",
     {
         {"vehicle_local_position", "vx", -1, 0, "vx"},
         {"vehicle_local_position", "vy", -1, 0, "vy"},
         {"vehicle_local_position", "vz", -1, 0, "vz"},
     }},
    {"Acceleration (m/s²)",
     "m/s²",
     {
         {"sensor_combined", "accelerometer_m_s2", 0, 0, "ax"},
         {"sensor_combined", "accelerometer_m_s2", 1, 0, "ay"},
         {"sensor_combined", "accelerometer_m_s2", 2, 0, "az"},
     }},
    {"Battery",
     "V / A",
     {
         {"battery_status", "voltage_v", -1, 0, "voltage (V)"},
         {"battery_status", "current_a", -1, 0, "current (A)"},
     }},
    {"Actuator Outputs",
     "norm",
     {
         {"actuator_outputs", "output", 0, 0, "motor 1"},
         {"actuator_outputs", "output", 1, 0, "motor 2"},
         {"actuator_outputs", "output", 2, 0, "motor 3"},
         {"actuator_outputs", "output", 3, 0, "motor 4"},
     }},
    {"Airspeed (m/s)",
     "m/s",
     {
         {"airspeed", "indicated_airspeed_m_s", -1, 0, "IAS"},
         {"airspeed", "true_airspeed_m_s", -1, 0, "TAS"},
     }},
};

}   // namespace

// ---------------------------------------------------------------------------
// parse_px4_args
// ---------------------------------------------------------------------------

Px4AppConfig parse_px4_args(int argc, char** argv, std::string& error_out)
{
    Px4AppConfig cfg;
    cfg.recent_files_path = Px4RecentFiles::default_path();

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h")
        {
            error_out = "Usage: spectra-px4 [options]\n"
                        "  --ulog FILE         Open ULog file on launch\n"
                        "  --host HOST         MAVLink UDP host (default 127.0.0.1)\n"
                        "  --port PORT         MAVLink UDP port (default 14540)\n"
                        "  --connect           Auto-connect to MAVLink on launch\n"
                        "  --window-s SEC      Time window for real-time plot (default 30)\n"
                        "  --help              Show this help\n";
            return cfg;
        }

        if (arg == "--ulog" && i + 1 < argc)
        {
            cfg.ulog_file = argv[++i];
        }
        else if (arg == "--host" && i + 1 < argc)
        {
            cfg.host = argv[++i];
        }
        else if (arg == "--port" && i + 1 < argc)
        {
            cfg.port = static_cast<uint16_t>(std::atoi(argv[++i]));
        }
        else if (arg == "--connect")
        {
            cfg.auto_connect = true;
        }
        else if (arg == "--window-s" && i + 1 < argc)
        {
            cfg.time_window_s = std::atof(argv[++i]);
        }
        else
        {
            // Unknown arg — treat as ULog file if it ends with .ulg.
            if (arg.size() > 4 && arg.substr(arg.size() - 4) == ".ulg")
                cfg.ulog_file = arg;
        }
    }

    return cfg;
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Px4AppShell::Px4AppShell(const Px4AppConfig& cfg)
#ifdef SPECTRA_USE_IMGUI
    : spectra::ui::shell::AppShell(
          spectra::ui::shell::AppShellConfig{.nav_rail = true, .app_name = "Spectra PX4"}),
      cfg_(cfg)
#else
    : cfg_(cfg)
#endif
{
#ifdef SPECTRA_USE_IMGUI
    file_chooser_ = choose_ulog_file_path;
#endif
    plots_.push_back(make_figure_plots(nullptr, spectra::INVALID_FIGURE_ID));
    plots_.front()->primary = true;
}

Px4AppShell::~Px4AppShell()
{
    shutdown();
}

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

bool Px4AppShell::init()
{
    for (auto& fp : plots_)
        fp->mgr.set_time_window(cfg_.time_window_s);

    file_panel_ = std::make_unique<ULogFilePanel>(reader_, active_plots().mgr);
    live_panel_ = std::make_unique<LiveConnectionPanel>(bridge_, active_plots().mgr);
    file_panel_->set_file_callback(
        [this](const std::string& path)
        {
            if (path.empty())
            {
                open_ulog_with_dialog();
                return;
            }

            open_ulog(path);
        });

    // Wire field callback: add to the active figure's plot on double-click.
    file_panel_->set_field_callback(
        [this](const std::string& topic, const std::string& field, int array_idx)
        {
            // Same path as a drop: on an auto-plot it joins the first subplot.
            drop_field(make_field_drag_payload(topic, field, array_idx));
        });

    // Load the recent-files list before the launch file is pushed onto it.
    recent_.set_path(cfg_.recent_files_path);
    if (!recent_.path().empty())
        recent_.load();
    recent_menu_dirty_ = true;

    // Open initial ULog file if specified.
    if (!cfg_.ulog_file.empty())
    {
        if (!open_ulog(cfg_.ulog_file))
        {
            SPECTRA_LOG_ERROR("px4", "failed to open ULog: {}", reader_.last_error());
        }
    }

    // Auto-connect if requested.
    if (cfg_.auto_connect)
        connect_live();

#ifdef SPECTRA_USE_IMGUI
    initialize();
#endif
    refresh_recent_menu();

    return true;
}

// ---------------------------------------------------------------------------
// connect_live
// ---------------------------------------------------------------------------

void Px4AppShell::connect_live()
{
    bridge_.init(cfg_.host, cfg_.port);
    bridge_.start();
    for (auto& fp : plots_)
        fp->mgr.set_bridge(&bridge_);
    set_panel_visible("px4.live_connection", true);
}

// ---------------------------------------------------------------------------
// shutdown
// ---------------------------------------------------------------------------

void Px4AppShell::shutdown()
{
    bridge_.shutdown();
    reader_.close();
}

// ---------------------------------------------------------------------------
// set_window_manager
// ---------------------------------------------------------------------------

void Px4AppShell::set_window_manager(spectra::WindowManager* wm)
{
    if (file_panel_)
        file_panel_->set_window_manager(wm);
    if (live_panel_)
        live_panel_->set_window_manager(wm);
    // OS file drops (GLFW drop callback / SDL_EVENT_DROP_FILE) open ULogs.
    // This replaces the main app's CSV drop handler: the PX4 viewer has no
    // CSV column picker.
    if (wm)
        wm->set_file_drop_handler([this](uint32_t /*window_id*/, const std::string& path)
                                  { handle_file_drop(path); });
}

// ---------------------------------------------------------------------------
// File drop
// ---------------------------------------------------------------------------

bool Px4AppShell::is_ulog_path(const std::string& path)
{
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(),
                   ext.end(),
                   ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".ulg" || ext == ".ulog";
}

bool Px4AppShell::handle_file_drop(const std::string& path)
{
    if (!is_ulog_path(path))
    {
        SPECTRA_LOG_WARN("px4", "ignoring dropped file (not a .ulg ULog): {}", path);
        last_open_error_ = "Dropped file is not a PX4 ULog (.ulg): " + path;
        return false;
    }
    // One log is open at a time: of several logs dropped together the last wins.
    pending_drop_path_ = path;
    return true;
}

bool Px4AppShell::process_pending_file_drop()
{
    if (pending_drop_path_.empty())
        return false;
    const std::string path = std::move(pending_drop_path_);
    pending_drop_path_.clear();
    const bool ok = open_ulog(path);
    if (ok)
        SPECTRA_LOG_INFO("px4", "opened dropped ULog: {}", path);
    else
        SPECTRA_LOG_WARN("px4", "failed to open dropped ULog {}: {}", path, last_open_error_);
    return ok;
}

// ---------------------------------------------------------------------------
// process_pending_panels — deferred OS window create/destroy
// ---------------------------------------------------------------------------

void Px4AppShell::process_pending_panels()
{
    if (file_panel_)
        file_panel_->process_pending();
    if (live_panel_)
        live_panel_->process_pending();
}

// ---------------------------------------------------------------------------
// poll
// ---------------------------------------------------------------------------

void Px4AppShell::poll()
{
    process_pending_file_drop();
    refresh_figure_targets();

    // Update live data if connected.
    if (bridge_.is_receiving())
        for (auto& fp : plots_)
            fp->mgr.poll();

    sync_canvas_figure();
    refresh_recent_menu();
}

// ---------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------

void Px4AppShell::draw()
{
#ifdef SPECTRA_USE_IMGUI
    if (shutdown_requested())
        return;

    sync_layout_chrome();
    // Rebuild File > Open Recent here, outside menu iteration: a menu click that
    // opens a file must not edit the item list it is being drawn from.
    refresh_recent_menu();
    draw_frame();
    draw_field_drop_target();
#endif
}

#ifdef SPECTRA_USE_IMGUI
bool Px4AppShell::panel_visible(const char* id) const
{
    if (!is_initialized())
    {
        const auto it = pending_panel_visibility_.find(id);
        if (it != pending_panel_visibility_.end())
            return it->second;
        return false;
    }
    const auto* panel = panels().find(id);
    return panel && panel->visible();
}

void Px4AppShell::set_panel_visible(const char* id, bool v)
{
    if (!is_initialized())
    {
        pending_panel_visibility_[id] = v;
        return;
    }
    panels().set_visible(id, v);
}

bool Px4AppShell::nav_rail_visible() const
{
    return config_.nav_rail;
}

bool Px4AppShell::nav_rail_expanded() const
{
    return nav_rail().expanded();
}

void Px4AppShell::set_nav_rail_visible(bool v)
{
    config_.nav_rail = v;
    sync_layout_chrome();
}

void Px4AppShell::set_nav_rail_expanded(bool v)
{
    nav_rail().set_expanded(v);
    sync_layout_chrome();
}

void Px4AppShell::set_layout_manager(spectra::LayoutManager* lm)
{
    AppShell::set_layout_manager(lm);
    sync_layout_chrome();
}

void Px4AppShell::sync_layout_chrome()
{
    if (spectra::LayoutManager* lm = layout_manager())
        lm->set_nav_rail_visible(config_.nav_rail);
}

void Px4AppShell::draw_ulog_file(bool* p_open)
{
    if (file_panel_)
        file_panel_->draw(p_open);
}

void Px4AppShell::draw_live_connection(bool* p_open)
{
    if (live_panel_)
        live_panel_->draw(p_open);
}
#else
bool Px4AppShell::panel_visible(const char*) const
{
    return false;
}

void Px4AppShell::set_panel_visible(const char*, bool) {}

bool Px4AppShell::nav_rail_visible() const
{
    return false;
}

bool Px4AppShell::nav_rail_expanded() const
{
    return false;
}

void Px4AppShell::set_nav_rail_visible(bool) {}

void Px4AppShell::set_nav_rail_expanded(bool) {}

void Px4AppShell::set_layout_manager(spectra::LayoutManager*) {}

void Px4AppShell::refresh_recent_menu()
{
    recent_menu_dirty_ = false;
}
#endif

// ---------------------------------------------------------------------------
// open_ulog
// ---------------------------------------------------------------------------

bool Px4AppShell::open_ulog(const std::string& path)
{
    if (path.empty())
    {
        last_open_error_ = "No ULog file was selected.";
        return false;
    }

    refresh_figure_targets();
    if (!reader_.open(path))
    {
        for (auto& fp : plots_)
        {
            fp->mgr.clear();
            fp->auto_groups.clear();
            fp->auto_active = false;
        }
        set_panel_visible("px4.ulog_file", true);
        last_open_error_ = reader_.last_error();
        sync_canvas_figure(true);
        return false;
    }

    for (auto& fp : plots_)
    {
        fp->mgr.load_ulog(reader_);
        // Auto-plots in the other figures keep their layout but show the new
        // log's samples; fields the new log lacks are dropped.
        if (fp->auto_active)
        {
            reload_auto_groups(*fp);
            sync_figure(*fp, true);
        }
    }
    set_panel_visible("px4.ulog_file", true);
    last_open_error_.clear();
    recent_.push(path);
    recent_menu_dirty_ = true;

    // Automatically generate flight-review-style plots on file open (in the
    // figure the user is looking at; other figures keep their fields, now
    // showing the new log's data).
    auto_plot_ulog();

    return true;
}

bool Px4AppShell::open_recent(const std::string& path)
{
    if (open_ulog(path))
        return true;

    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
    {
        recent_.remove(path);
        recent_menu_dirty_ = true;
    }
    return false;
}

void Px4AppShell::clear_recent_files()
{
    recent_.clear();
    recent_menu_dirty_ = true;
}

void Px4AppShell::set_file_chooser(std::function<std::string()> chooser)
{
    file_chooser_ = std::move(chooser);
}

// ---------------------------------------------------------------------------
// auto_plot_ulog
// ---------------------------------------------------------------------------

void Px4AppShell::auto_plot_ulog()
{
    if (!reader_.is_open())
        return;

    refresh_figure_targets();
    FigurePlots& fp = active_plots();

    // Clear existing state.
    fp.auto_groups.clear();
    fp.mgr.clear();
    fp.mgr.load_ulog(reader_);
    if (bridge_.is_connected())
        fp.mgr.set_bridge(&bridge_);

    const auto topics       = fp.mgr.available_topics();
    auto       topic_exists = [&](const char* t)
    { return std::find(topics.begin(), topics.end(), std::string(t)) != topics.end(); };

    for (const auto& gdef : k_auto_plot_groups)
    {
        AutoPlotGroup group;
        group.title  = gdef.title;
        group.ylabel = gdef.ylabel;

        for (const auto& fdef : gdef.fields)
        {
            if (!topic_exists(fdef.topic))
                continue;

            // Verify the field exists in this topic.
            const auto  fields    = fp.mgr.topic_fields(fdef.topic);
            std::string field_key = fdef.array_idx >= 0 ? std::string(fdef.field) + "["
                                                              + std::to_string(fdef.array_idx) + "]"
                                                        : std::string(fdef.field);
            bool        found = std::find(fields.begin(), fields.end(), field_key) != fields.end();
            if (!found)
                continue;

            // Load data directly from reader.
            auto* ts = reader_.data_for(fdef.topic, fdef.multi_id);
            if (!ts)
                continue;

            AutoPlotField apf;
            apf.topic     = fdef.topic;
            apf.field     = fdef.field;
            apf.array_idx = fdef.array_idx;
            apf.multi_id  = fdef.multi_id;
            apf.label =
                fdef.label_override
                    ? fdef.label_override
                    : (fdef.array_idx >= 0 ? std::string(fdef.topic) + "." + fdef.field + "["
                                                 + std::to_string(fdef.array_idx) + "]"
                                           : std::string(fdef.topic) + "." + fdef.field);

            if (fdef.array_idx >= 0)
            {
                auto [times, values] = ts->extract_array_element(fdef.field, fdef.array_idx);
                apf.times            = std::move(times);
                apf.values           = std::move(values);
            }
            else
            {
                auto [times, values] = ts->extract_field(fdef.field);
                apf.times            = std::move(times);
                apf.values           = std::move(values);
            }

            if (!apf.times.empty())
                group.fields.push_back(std::move(apf));
        }

        if (!group.fields.empty())
            fp.auto_groups.push_back(std::move(group));
    }

    fp.auto_active = !fp.auto_groups.empty();
    sync_figure(fp, true);
}

// Re-extract every auto-plot field from the open log, dropping fields (and
// then groups) the log doesn't have.
void Px4AppShell::reload_auto_groups(FigurePlots& fp)
{
    for (auto& group : fp.auto_groups)
    {
        std::vector<AutoPlotField> kept;
        for (auto& apf : group.fields)
        {
            const auto* ts = reader_.data_for(apf.topic, apf.multi_id);
            if (!ts)
                continue;
            auto [times, values] = apf.array_idx >= 0
                                       ? ts->extract_array_element(apf.field, apf.array_idx)
                                       : ts->extract_field(apf.field);
            if (times.empty())
                continue;
            apf.times  = std::move(times);
            apf.values = std::move(values);
            kept.push_back(std::move(apf));
        }
        group.fields = std::move(kept);
    }
    std::erase_if(fp.auto_groups, [](const AutoPlotGroup& g) { return g.fields.empty(); });
    fp.auto_active = !fp.auto_groups.empty();
}

// ---------------------------------------------------------------------------
// close_all_plots
// ---------------------------------------------------------------------------

void Px4AppShell::close_all_plots()
{
    refresh_figure_targets();
    FigurePlots& fp = active_plots();
    fp.auto_groups.clear();
    fp.auto_active = false;
    fp.mgr.clear();
    if (reader_.is_open())
        fp.mgr.load_ulog(reader_);
    if (bridge_.is_connected())
        fp.mgr.set_bridge(&bridge_);
    sync_figure(fp, true);
}

bool Px4AppShell::empty_state_visible() const
{
    if (reader_.is_open() || bridge_.is_connected())
        return false;
    return std::none_of(plots_.begin(),
                        plots_.end(),
                        [](const auto& fp)
                        { return fp->auto_active || fp->mgr.field_count() > 0; });
}

// ---------------------------------------------------------------------------
// Figure targets — one plot set per figure (new figures, split panes).
// ---------------------------------------------------------------------------

void Px4AppShell::set_canvas_figure(spectra::Figure* fig)
{
    FigurePlots& primary  = *plots_.front();
    primary.figure        = fig;
    primary.id            = spectra::INVALID_FIGURE_ID;
    primary.last_revision = static_cast<uint64_t>(-1);
    refresh_figure_targets();
}

void Px4AppShell::set_figure_source(spectra::FigureRegistry*           registry,
                                    std::function<spectra::FigureId()> active_figure)
{
    figure_registry_  = registry;
    active_figure_fn_ = std::move(active_figure);
    refresh_figure_targets();
}

void Px4AppShell::set_figure_hit_test(std::function<spectra::FigureId(float, float)> figure_at)
{
    figure_at_fn_ = std::move(figure_at);
}

Px4AppShell::FigurePlots* Px4AppShell::plots_for(spectra::Figure* fig)
{
    if (!fig)
        return nullptr;
    for (auto& fp : plots_)
        if (fp->figure == fig)
            return fp.get();
    if (!figure_registry_)
        return nullptr;
    const spectra::FigureId id = figure_registry_->find_id(fig);
    if (figure_registry_->get(id) != fig)
        return nullptr;
    plots_.push_back(make_figure_plots(fig, id));
    return plots_.back().get();
}

bool Px4AppShell::drop_field(const Px4FieldDragPayload& payload,
                             spectra::Figure*           figure,
                             int                        axes_index)
{
    refresh_figure_targets();
    FigurePlots* fp = figure ? plots_for(figure) : &active_plots();
    if (!fp)
        return false;

    // The payload buffers are always NUL-terminated.
    const std::string topic(payload.topic);
    const std::string field(payload.field);
    if (topic.empty())
        return false;

    // Expand the drop to the fields it names: one field, or a whole topic.
    std::vector<FieldKey> keys;
    if (!field.empty())
        keys.push_back({field, payload.array_idx});
    else if (payload.live)
    {
        if (auto latest = bridge_.channel_latest(topic))
            for (const auto& f : latest->fields)
                keys.push_back({f.name, -1});
    }
    else
    {
        for (const auto& k : fp->mgr.topic_fields(topic))
            if (k != "timestamp")
                keys.push_back(parse_field_key(k));
    }
    if (keys.empty())
        return false;

    if (fp->auto_active)
    {
        // Auto-plot figure: the field joins the subplot it was dropped on.
        if (payload.live || fp->auto_groups.empty())
            return false;
        const size_t gi =
            axes_index >= 0 && static_cast<size_t>(axes_index) < fp->auto_groups.size()
                ? static_cast<size_t>(axes_index)
                : 0;
        const auto* ts = reader_.data_for(topic, payload.multi_id);
        if (!ts)
            return false;
        auto& group = fp->auto_groups[gi];
        bool  added = false;
        for (const auto& k : keys)
        {
            AutoPlotField apf;
            apf.topic     = topic;
            apf.field     = k.name;
            apf.array_idx = k.array_idx;
            apf.multi_id  = payload.multi_id;
            apf.label     = topic + "." + k.name
                        + (k.array_idx >= 0 ? "[" + std::to_string(k.array_idx) + "]" : "");
            auto [times, values] = k.array_idx >= 0 ? ts->extract_array_element(k.name, k.array_idx)
                                                    : ts->extract_field(k.name);
            if (times.empty())
                continue;
            apf.times  = std::move(times);
            apf.values = std::move(values);
            group.fields.push_back(std::move(apf));
            added = true;
        }
        if (added)
            sync_figure(*fp, true);
        return added;
    }

    for (const auto& k : keys)
    {
        if (payload.live)
            fp->mgr.add_live_field(topic, k.name);
        else
            fp->mgr.add_field(topic, k.name, k.array_idx, payload.multi_id);
    }
    sync_figure(*fp, true);
    return true;
}

spectra::Figure* Px4AppShell::target_figure() const
{
    return active_plots().figure;
}

std::unique_ptr<Px4AppShell::FigurePlots> Px4AppShell::make_figure_plots(spectra::Figure*  fig,
                                                                         spectra::FigureId id)
{
    auto fp    = std::make_unique<FigurePlots>();
    fp->figure = fig;
    fp->id     = id;
    fp->mgr.set_time_window(cfg_.time_window_s);
    if (reader_.is_open())
        fp->mgr.load_ulog(reader_);
    if (bridge_.is_connected())
        fp->mgr.set_bridge(&bridge_);
    return fp;
}

void Px4AppShell::refresh_figure_targets()
{
    const Px4PlotManager* prev_mgr = &active_plots().mgr;

    if (figure_registry_)
    {
        // Learn the canvas figure's id, then drop plot sets of closed figures.
        FigurePlots& primary = *plots_.front();
        if (primary.id == spectra::INVALID_FIGURE_ID && primary.figure)
        {
            const spectra::FigureId id = figure_registry_->find_id(primary.figure);
            if (figure_registry_->get(id) == primary.figure)
                primary.id = id;
        }
        for (auto& fp : plots_)
            if (fp->id != spectra::INVALID_FIGURE_ID)
                fp->figure = figure_registry_->get(fp->id);
        // The canvas figure's set always stays (empty state, panels); a
        // closed canvas figure just stops being drawn into.
        plots_.erase(std::remove_if(plots_.begin() + 1,
                                    plots_.end(),
                                    [](const auto& fp) { return fp->figure == nullptr; }),
                     plots_.end());

        active_plots_ = 0;
        const spectra::FigureId active =
            active_figure_fn_ ? active_figure_fn_() : spectra::INVALID_FIGURE_ID;
        spectra::Figure* active_fig =
            active != spectra::INVALID_FIGURE_ID ? figure_registry_->get(active) : nullptr;
        if (active_fig)
        {
            auto it = std::find_if(plots_.begin(),
                                   plots_.end(),
                                   [&](const auto& fp) { return fp->figure == active_fig; });
            if (it == plots_.end())
            {
                plots_.push_back(make_figure_plots(active_fig, active));
                it = plots_.end() - 1;
            }
            active_plots_ = static_cast<size_t>(it - plots_.begin());
        }
    }
    else
    {
        active_plots_ = 0;
    }

    // Panels add fields to (and list them from) the active figure's set.
    Px4PlotManager* mgr = &active_plots().mgr;
    if (mgr != prev_mgr)
    {
        if (file_panel_)
            file_panel_->set_plot_manager(*mgr);
        if (live_panel_)
            live_panel_->set_plot_manager(*mgr);
    }
}

void Px4AppShell::sync_canvas_figure(bool force)
{
    for (auto& fp : plots_)
        sync_figure(*fp, force);
}

void Px4AppShell::sync_figure(FigurePlots& fp, bool force)
{
    spectra::Figure* fig = fp.figure;
    if (!fig)
        return;

    if (!fp.primary && !fp.auto_active && fp.mgr.field_count() == 0
        && fp.last_revision == static_cast<uint64_t>(-1))
        return;   // a new figure keeps its own (empty) axes until something is plotted

    if (fp.primary && empty_state_visible())
    {
        // Nothing to plot: drop all axes so the canvas shows the empty state
        // rather than a meaningless 0-1 grid.
        if (!fig->axes().empty())
        {
            rebuild_figure_axes(fp, 1, 1);
            fig->legend().visible = false;
        }
        fp.last_revision = static_cast<uint64_t>(-1);
        return;
    }

    if (fp.auto_active)
        sync_auto_plot_figure(fp);
    else
        sync_manual_plot_figure(fp, force);
}

// ---------------------------------------------------------------------------
// rebuild_figure_axes — safely reset figure to a fresh rows×cols grid.
// ---------------------------------------------------------------------------

void Px4AppShell::rebuild_figure_axes(FigurePlots& fp, int rows, int cols)
{
    spectra::Figure* fig = fp.figure;
    // Clear all existing series (GPU-safe deferred cleanup).
    for (auto& ax : fig->axes_mut())
        if (ax)
            ax->clear_series();

    // Drop the axes vector and reset the grid.
    fig->axes_mut().clear();
    fig->set_grid(rows, cols);
    fp.last_revision = static_cast<uint64_t>(-1);
}

std::pair<int, int> Px4AppShell::auto_plot_grid(int num_groups)
{
    if (num_groups <= 3)
        return {std::max(1, num_groups), 1};
    // Tile into columns so each plot keeps a usable height instead of stacking
    // every group into one column that overflows the canvas.  Rows are capped
    // at three for up to nine groups; beyond that the canvas scrolls.
    const int cols = num_groups <= 6 ? 2 : 3;
    const int rows = (num_groups + cols - 1) / cols;
    return {rows, cols};
}

// ---------------------------------------------------------------------------
// sync_auto_plot_figure — multi-subplot layout for auto-plot mode.
// ---------------------------------------------------------------------------

void Px4AppShell::sync_auto_plot_figure(FigurePlots& fp)
{
    spectra::Figure* fig = fp.figure;
    if (!fig)
        return;

    const uint64_t revision     = fp.mgr.revision();
    const bool     data_changed = (revision != fp.last_revision);

    const int n = static_cast<int>(fp.auto_groups.size());
    if (n == 0)
    {
        rebuild_figure_axes(fp, 1, 1);
        auto& ax = fig->subplot(1, 1, 1);
        ax.title("PX4 ULog — no data");
        ax.xlabel("time (s)");
        fig->legend().visible = false;
        fp.last_revision      = revision;
        return;
    }

    // Ensure the axes vector is properly sized for the current group count.
    const auto [rows, cols] = auto_plot_grid(n);
    if (fig->grid_rows() != rows || fig->grid_cols() != cols
        || static_cast<int>(fig->axes().size()) != n)
    {
        rebuild_figure_axes(fp, rows, cols);
        // The default 150 px minimum per row makes three rows of plots (plus
        // title / tick margins) taller than a typical canvas; let the grid
        // shrink to the canvas instead of overflowing it.
        fig->style().min_subplot_height =
            std::min(fig->style().min_subplot_height, kAutoPlotMinSubplotHeight);
    }

    for (int i = 0; i < n; ++i)
    {
        const auto& group = fp.auto_groups[static_cast<size_t>(i)];
        auto&       ax    = fig->subplot(rows, cols, i + 1);

        ax.title(group.title);
        ax.xlabel("time (s)");
        ax.ylabel(group.ylabel);
        ax.grid(true);
        ax.show_border(true);

        // Rebuild series for this subplot if count changed.
        const bool needs_rebuild = ax.series().size() != group.fields.size();

        if (needs_rebuild)
        {
            ax.clear_series();
            for (const auto& f : group.fields)
            {
                if (cfg_.use_chunked)
                {
                    auto& cs = ax.chunked_line();
                    cs.set_data(f.times, f.values);
                    cs.label(f.label);
                    cs.enable_lod(true);
                    if (cfg_.chunked_memory_budget > 0)
                        cs.set_memory_budget(cfg_.chunked_memory_budget);
                }
                else
                {
                    auto& ls = ax.line(f.times, f.values);
                    ls.label(f.label);
                }
            }
        }
        else if (data_changed)
        {
            for (size_t j = 0; j < group.fields.size(); ++j)
            {
                const auto& f    = group.fields[j];
                auto*       sptr = ax.series_mut()[j].get();

                // Try ChunkedLineSeries first (when chunked mode active).
                auto* chunked = dynamic_cast<spectra::ChunkedLineSeries*>(sptr);
                if (chunked)
                {
                    chunked->set_data(f.times, f.values);
                    chunked->label(f.label);
                    continue;
                }

                auto* line = dynamic_cast<spectra::LineSeries*>(sptr);
                if (!line)
                {
                    ax.clear_series();
                    for (const auto& rf : group.fields)
                    {
                        auto& ls = ax.line(rf.times, rf.values);
                        ls.label(rf.label);
                    }
                    break;
                }
                line->set_x(f.times);
                line->set_y(f.values);
                line->label(f.label);
            }
        }

        // Only auto-fit when data actually changed, so user zoom is preserved.
        if (!group.fields.empty() && (needs_rebuild || data_changed))
            ax.auto_fit();
    }

    fig->legend().visible = true;
    fp.last_revision      = revision;
}

// ---------------------------------------------------------------------------
// sync_manual_plot_figure — single subplot for manually added fields.
// ---------------------------------------------------------------------------

void Px4AppShell::sync_manual_plot_figure(FigurePlots& fp, bool force)
{
    spectra::Figure* fig = fp.figure;
    if (!fig)
        return;

    const uint64_t revision = fp.mgr.revision();
    if (!force && revision == fp.last_revision)
        return;

    // Ensure we are back to a 1×1 grid if coming from auto-plot.
    if (fig->grid_rows() != 1 || fig->grid_cols() != 1 || fig->axes().size() != 1)
        rebuild_figure_axes(fp, 1, 1);

    auto& ax = fig->subplot(1, 1, 1);
    ax.xlabel("time (s)");
    ax.grid(true);
    ax.show_border(true);

    if (fp.mgr.field_count() == 1)
        ax.ylabel(fp.mgr.fields().front().label);
    else
        ax.ylabel("value");

    if (bridge_.is_receiving())
        ax.title("PX4 Live Telemetry");
    else if (reader_.is_open())
        ax.title("PX4 ULog Fields");
    else
        ax.title("PX4");

    const bool needs_rebuild = ax.series().size() != fp.mgr.field_count();
    if (needs_rebuild)
    {
        ax.clear_series();
        for (const auto& field : fp.mgr.fields())
        {
            if (cfg_.use_chunked)
            {
                auto& cs = ax.chunked_line();
                cs.set_data(field.times, field.values);
                cs.label(field.label);
                cs.enable_lod(true);
                cs.visible(field.visible);
                if (cfg_.chunked_memory_budget > 0)
                    cs.set_memory_budget(cfg_.chunked_memory_budget);
            }
            else
            {
                auto& ls = ax.line(field.times, field.values);
                ls.label(field.label);
                ls.visible(field.visible);
            }
        }
    }
    else
    {
        for (size_t i = 0; i < fp.mgr.fields().size(); ++i)
        {
            const auto& field = fp.mgr.fields()[i];
            auto*       sptr  = ax.series_mut()[i].get();

            // Try ChunkedLineSeries first (when chunked mode active).
            auto* chunked = dynamic_cast<spectra::ChunkedLineSeries*>(sptr);
            if (chunked)
            {
                chunked->set_data(field.times, field.values);
                chunked->label(field.label);
                chunked->visible(field.visible);
                continue;
            }

            auto* line = dynamic_cast<spectra::LineSeries*>(sptr);
            if (!line)
            {
                ax.clear_series();
                for (const auto& rebuild_field : fp.mgr.fields())
                {
                    auto& ls = ax.line(rebuild_field.times, rebuild_field.values);
                    ls.label(rebuild_field.label);
                    ls.visible(rebuild_field.visible);
                }
                break;
            }

            line->set_x(field.times);
            line->set_y(field.values);
            line->label(field.label);
            line->visible(field.visible);
        }
    }

    if (fp.mgr.field_count() > 0)
        ax.auto_fit();

    fig->legend().visible = fp.mgr.field_count() > 0;
    fp.last_revision      = revision;
}

void Px4AppShell::draw_field_drop_target()
{
#ifdef SPECTRA_USE_IMGUI
    const ImGuiPayload* active_payload = ImGui::GetDragDropPayload();
    if (!active_payload || !active_payload->IsDataType(PX4_FIELD_DRAG_TYPE))
        return;
    spectra::LayoutManager* lm = layout_manager();
    if (!lm)
        return;
    const spectra::Rect bounds = lm->canvas_rect();
    if (bounds.w < 1.0f || bounds.h < 1.0f)
        return;

    // Figure (split pane) and subplot under the cursor.
    const ImVec2     mouse  = ImGui::GetIO().MousePos;
    spectra::Figure* figure = target_figure();
    if (figure_at_fn_ && figure_registry_)
    {
        const spectra::FigureId id = figure_at_fn_(mouse.x, mouse.y);
        if (id != spectra::INVALID_FIGURE_ID)
            if (spectra::Figure* f = figure_registry_->get(id))
                figure = f;
    }
    int                  axes_index = -1;
    const spectra::Rect* hover_vp   = nullptr;
    if (figure)
    {
        const auto& axes = figure->axes();
        for (size_t i = 0; i < axes.size(); ++i)
        {
            if (!axes[i])
                continue;
            const spectra::Rect& vp = axes[i]->viewport();
            if (mouse.x >= vp.x && mouse.x < vp.x + vp.w && mouse.y >= vp.y
                && mouse.y < vp.y + vp.h)
            {
                axes_index = static_cast<int>(i);
                hover_vp   = &vp;
                break;
            }
        }
    }

    // Highlight where the drop lands: the hovered subplot, else the canvas.
    const spectra::Rect hl     = hover_vp ? *hover_vp : bounds;
    const auto&         accent = spectra::ui::theme().accent;
    ImDrawList*         dl     = spectra::ui::canvas_overlay_draw_list();
    const ImU32 fill = ImGui::ColorConvertFloat4ToU32(ImVec4(accent.r, accent.g, accent.b, 0.16f));
    const ImU32 border =
        ImGui::ColorConvertFloat4ToU32(ImVec4(accent.r, accent.g, accent.b, 0.85f));
    dl->AddRectFilled(ImVec2(hl.x, hl.y), ImVec2(hl.x + hl.w, hl.y + hl.h), fill, 4.0f);
    dl->AddRect(ImVec2(hl.x, hl.y), ImVec2(hl.x + hl.w, hl.y + hl.h), border, 4.0f, 0, 2.0f);

    // Invisible drop window over the canvas, only while a px4 field is dragged.
    ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y));
    ImGui::SetNextWindowSize(ImVec2(bounds.w, bounds.h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground
        | ImGuiWindowFlags_NoDocking;
    if (ImGui::Begin("##px4_field_drop", nullptr, flags))
    {
        ImGui::InvisibleButton("##px4_drop_area", ImVec2(bounds.w, bounds.h));
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* p =
                    ImGui::AcceptDragDropPayload(PX4_FIELD_DRAG_TYPE,
                                                 ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
                if (p->DataSize == static_cast<int>(sizeof(Px4FieldDragPayload)))
                    drop_field(*static_cast<const Px4FieldDragPayload*>(p->Data),
                               figure,
                               axes_index);
            ImGui::EndDragDropTarget();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
#endif
}

bool Px4AppShell::open_ulog_with_dialog()
{
    const std::string path = file_chooser_ ? file_chooser_() : std::string();
    if (path.empty())
        return false;

    return open_ulog(path);
}

}   // namespace spectra::adapters::px4
