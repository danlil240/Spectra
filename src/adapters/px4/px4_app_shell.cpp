#include "px4_app_shell.hpp"

#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>

#ifdef SPECTRA_USE_IMGUI
#include <imgui.h>
#include "../../../third_party/tinyfiledialogs.h"
#endif

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace spectra::adapters::px4
{

namespace
{

#ifdef SPECTRA_USE_IMGUI
std::string choose_ulog_file_path()
{
    char const* filter_patterns[] = {"*.ulg", "*.ulog"};
    const char* home_env = std::getenv("HOME");
    std::string default_path = home_env ? std::string(home_env) + "/" : "/";
    const char* result = tinyfd_openFileDialog(
        "Open ULog", default_path.c_str(), 2, filter_patterns, "PX4 ULog (*.ulg)", 0);

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
    const char*             title;
    const char*             ylabel;
    std::vector<FieldDef>   fields;
};

static const GroupDef k_auto_plot_groups[] = {
    {
        "Attitude (deg)",
        "deg",
        {
            {"vehicle_attitude_setpoint", "roll_body",  -1, 0, "roll sp"},
            {"vehicle_attitude_setpoint", "pitch_body", -1, 0, "pitch sp"},
            {"vehicle_attitude_setpoint", "yaw_body",   -1, 0, "yaw sp"},
        }
    },
    {
        "Angular Velocity (rad/s)",
        "rad/s",
        {
            {"vehicle_angular_velocity", "xyz", 0, 0, "roll rate"},
            {"vehicle_angular_velocity", "xyz", 1, 0, "pitch rate"},
            {"vehicle_angular_velocity", "xyz", 2, 0, "yaw rate"},
            {"vehicle_rates_setpoint",   "roll",  -1, 0, "roll rate sp"},
            {"vehicle_rates_setpoint",   "pitch", -1, 0, "pitch rate sp"},
            {"vehicle_rates_setpoint",   "yaw",   -1, 0, "yaw rate sp"},
        }
    },
    {
        "Altitude (m)",
        "m",
        {
            {"vehicle_local_position",         "z",  -1, 0, "altitude (NED z)"},
            {"vehicle_local_position_setpoint", "z", -1, 0, "altitude sp"},
        }
    },
    {
        "Velocity (m/s)",
        "m/s",
        {
            {"vehicle_local_position", "vx", -1, 0, "vx"},
            {"vehicle_local_position", "vy", -1, 0, "vy"},
            {"vehicle_local_position", "vz", -1, 0, "vz"},
        }
    },
    {
        "Acceleration (m/s²)",
        "m/s²",
        {
            {"sensor_combined", "accelerometer_m_s2", 0, 0, "ax"},
            {"sensor_combined", "accelerometer_m_s2", 1, 0, "ay"},
            {"sensor_combined", "accelerometer_m_s2", 2, 0, "az"},
        }
    },
    {
        "Battery",
        "V / A",
        {
            {"battery_status", "voltage_v",  -1, 0, "voltage (V)"},
            {"battery_status", "current_a",  -1, 0, "current (A)"},
        }
    },
    {
        "Actuator Outputs",
        "norm",
        {
            {"actuator_outputs", "output", 0, 0, "motor 1"},
            {"actuator_outputs", "output", 1, 0, "motor 2"},
            {"actuator_outputs", "output", 2, 0, "motor 3"},
            {"actuator_outputs", "output", 3, 0, "motor 4"},
        }
    },
    {
        "Airspeed (m/s)",
        "m/s",
        {
            {"airspeed", "indicated_airspeed_m_s", -1, 0, "IAS"},
            {"airspeed", "true_airspeed_m_s",      -1, 0, "TAS"},
        }
    },
};

}   // namespace

// ---------------------------------------------------------------------------
// parse_px4_args
// ---------------------------------------------------------------------------

Px4AppConfig parse_px4_args(int argc, char** argv, std::string& error_out)
{
    Px4AppConfig cfg;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h")
        {
            error_out =
                "Usage: spectra-px4 [options]\n"
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
    : cfg_(cfg)
{
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
    plot_mgr_.set_time_window(cfg_.time_window_s);

    file_panel_ = std::make_unique<ULogFilePanel>(reader_, plot_mgr_);
    live_panel_ = std::make_unique<LiveConnectionPanel>(bridge_, plot_mgr_);
    file_panel_->set_file_callback([this](const std::string& path)
    {
        if (path.empty())
        {
            open_ulog_with_dialog();
            return;
        }

        open_ulog(path);
    });

    // Wire field callback: add to plot on double-click.
    file_panel_->set_field_callback(
        [this](const std::string& topic, const std::string& field, int array_idx)
        {
            plot_mgr_.add_field(topic, field, array_idx);
            sync_canvas_figure();
        });

    // Open initial ULog file if specified.
    if (!cfg_.ulog_file.empty())
    {
        if (!open_ulog(cfg_.ulog_file))
        {
            std::fprintf(stderr, "spectra-px4: failed to open ULog: %s\n",
                         reader_.last_error().c_str());
        }
    }

    // Auto-connect if requested.
    if (cfg_.auto_connect)
    {
        bridge_.init(cfg_.host, cfg_.port);
        bridge_.start();
        plot_mgr_.set_bridge(&bridge_);
        show_live_panel_ = true;
    }

    return true;
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
// poll
// ---------------------------------------------------------------------------

void Px4AppShell::poll()
{
    // Update live data if connected.
    if (bridge_.is_receiving())
        plot_mgr_.poll();

    sync_canvas_figure();
}

// ---------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------

void Px4AppShell::draw()
{
#ifdef SPECTRA_USE_IMGUI
    draw_menu_bar();

    if (show_file_panel_ && file_panel_)
        file_panel_->draw(&show_file_panel_);

    if (show_live_panel_ && live_panel_)
        live_panel_->draw(&show_live_panel_);

    draw_status_bar();
#endif
}

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

    if (!reader_.open(path))
    {
        plot_mgr_.clear();
        show_file_panel_ = true;
        last_open_error_ = reader_.last_error();
        sync_canvas_figure(true);
        return false;
    }

    plot_mgr_.load_ulog(reader_);
    show_file_panel_ = true;
    last_open_error_.clear();

    // Automatically generate flight-review-style plots on file open.
    auto_plot_ulog();

    return true;
}

// ---------------------------------------------------------------------------
// auto_plot_ulog
// ---------------------------------------------------------------------------

void Px4AppShell::auto_plot_ulog()
{
    if (!reader_.is_open())
        return;

    // Clear existing state.
    auto_plot_groups_.clear();
    plot_mgr_.clear();
    plot_mgr_.load_ulog(reader_);

    const auto topics = plot_mgr_.available_topics();
    auto topic_exists = [&](const char* t) {
        return std::find(topics.begin(), topics.end(), std::string(t)) != topics.end();
    };

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
            const auto fields = plot_mgr_.topic_fields(fdef.topic);
            std::string field_key = fdef.array_idx >= 0
                ? std::string(fdef.field) + "[" + std::to_string(fdef.array_idx) + "]"
                : std::string(fdef.field);
            bool found = std::find(fields.begin(), fields.end(), field_key) != fields.end();
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
            apf.label     = fdef.label_override ? fdef.label_override
                            : (fdef.array_idx >= 0
                               ? std::string(fdef.topic) + "." + fdef.field
                                     + "[" + std::to_string(fdef.array_idx) + "]"
                               : std::string(fdef.topic) + "." + fdef.field);

            if (fdef.array_idx >= 0)
            {
                auto [times, values] = ts->extract_array_element(fdef.field, fdef.array_idx);
                apf.times  = std::move(times);
                apf.values = std::move(values);
            }
            else
            {
                auto [times, values] = ts->extract_field(fdef.field);
                apf.times  = std::move(times);
                apf.values = std::move(values);
            }

            if (!apf.times.empty())
                group.fields.push_back(std::move(apf));
        }

        if (!group.fields.empty())
            auto_plot_groups_.push_back(std::move(group));
    }

    auto_plot_active_ = !auto_plot_groups_.empty();
    sync_canvas_figure(true);
}

// ---------------------------------------------------------------------------
// close_all_plots
// ---------------------------------------------------------------------------

void Px4AppShell::close_all_plots()
{
    auto_plot_groups_.clear();
    auto_plot_active_ = false;
    plot_mgr_.clear();
    if (reader_.is_open())
        plot_mgr_.load_ulog(reader_);
    sync_canvas_figure(true);
}

void Px4AppShell::sync_canvas_figure(bool force)
{
    if (!canvas_figure_)
        return;

    if (auto_plot_active_)
    {
        sync_auto_plot_figure();
    }
    else
    {
        sync_manual_plot_figure(force);
    }
}

// ---------------------------------------------------------------------------
// rebuild_figure_axes — safely reset figure to a fresh N×1 grid.
// ---------------------------------------------------------------------------

void Px4AppShell::rebuild_figure_axes(int num_groups)
{
    // Clear all existing series (GPU-safe deferred cleanup).
    for (auto& ax : canvas_figure_->axes_mut())
        if (ax) ax->clear_series();

    // Drop the axes vector and reset the grid.
    canvas_figure_->axes_mut().clear();
    canvas_figure_->set_grid(num_groups, 1);
    last_canvas_revision_ = static_cast<uint64_t>(-1);
}

// ---------------------------------------------------------------------------
// sync_auto_plot_figure — multi-subplot layout for auto-plot mode.
// ---------------------------------------------------------------------------

void Px4AppShell::sync_auto_plot_figure()
{
    if (!canvas_figure_)
        return;

    const int n = static_cast<int>(auto_plot_groups_.size());
    if (n == 0)
    {
        rebuild_figure_axes(1);
        auto& ax = canvas_figure_->subplot(1, 1, 1);
        ax.title("PX4 ULog — no data");
        ax.xlabel("time (s)");
        canvas_figure_->legend().visible = false;
        return;
    }

    // Ensure the axes vector is properly sized for the current group count.
    const int cur_rows = canvas_figure_->grid_rows();
    if (cur_rows != n || static_cast<int>(canvas_figure_->axes().size()) != n)
        rebuild_figure_axes(n);

    for (int i = 0; i < n; ++i)
    {
        const auto& group = auto_plot_groups_[static_cast<size_t>(i)];
        auto& ax = canvas_figure_->subplot(n, 1, i + 1);

        ax.title(group.title);
        ax.xlabel("time (s)");
        ax.ylabel(group.ylabel);
        ax.grid(true);
        ax.show_border(true);

        // Rebuild series for this subplot if count changed.
        const bool needs_rebuild =
            ax.series().size() != group.fields.size();

        if (needs_rebuild)
        {
            ax.clear_series();
            for (const auto& f : group.fields)
            {
                auto& ls = ax.line(f.times, f.values);
                ls.label(f.label);
            }
        }
        else
        {
            for (size_t j = 0; j < group.fields.size(); ++j)
            {
                const auto& f    = group.fields[j];
                auto*        line = dynamic_cast<spectra::LineSeries*>(
                    ax.series_mut()[j].get());
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

        if (!group.fields.empty())
            ax.auto_fit();
    }

    canvas_figure_->legend().visible = true;
}

// ---------------------------------------------------------------------------
// sync_manual_plot_figure — single subplot for manually added fields.
// ---------------------------------------------------------------------------

void Px4AppShell::sync_manual_plot_figure(bool force)
{
    if (!canvas_figure_)
        return;

    const uint64_t revision = plot_mgr_.revision();
    if (!force && revision == last_canvas_revision_)
        return;

    // Ensure we are back to a 1×1 grid if coming from auto-plot.
    if (canvas_figure_->grid_rows() != 1 || canvas_figure_->axes().size() != 1)
        rebuild_figure_axes(1);

    auto& ax = canvas_figure_->subplot(1, 1, 1);
    ax.xlabel("time (s)");
    ax.grid(true);
    ax.show_border(true);

    if (plot_mgr_.field_count() == 1)
        ax.ylabel(plot_mgr_.fields().front().label);
    else
        ax.ylabel("value");

    if (bridge_.is_receiving())
        ax.title("PX4 Live Telemetry");
    else if (reader_.is_open())
        ax.title("PX4 ULog Fields");
    else
        ax.title("PX4");

    const bool needs_rebuild = ax.series().size() != plot_mgr_.field_count();
    if (needs_rebuild)
    {
        ax.clear_series();
        for (const auto& field : plot_mgr_.fields())
        {
            auto& ls = ax.line(field.times, field.values);
            ls.label(field.label);
            ls.visible(field.visible);
        }
    }
    else
    {
        for (size_t i = 0; i < plot_mgr_.fields().size(); ++i)
        {
            const auto& field = plot_mgr_.fields()[i];
            auto* line = dynamic_cast<spectra::LineSeries*>(ax.series_mut()[i].get());
            if (!line)
            {
                ax.clear_series();
                for (const auto& rebuild_field : plot_mgr_.fields())
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

    if (plot_mgr_.field_count() > 0)
        ax.auto_fit();

    canvas_figure_->legend().visible = plot_mgr_.field_count() > 0;
    last_canvas_revision_            = revision;
}

bool Px4AppShell::open_ulog_with_dialog()
{
#ifdef SPECTRA_USE_IMGUI
    const std::string path = choose_ulog_file_path();
    if (path.empty())
        return false;

    return open_ulog(path);
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// draw_menu_bar
// ---------------------------------------------------------------------------

void Px4AppShell::draw_menu_bar()
{
#ifdef SPECTRA_USE_IMGUI
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Open ULog...", "Ctrl+O"))
            {
                open_ulog_with_dialog();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Ctrl+Q"))
                request_shutdown();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Plots"))
        {
            const bool has_ulog = reader_.is_open();
            if (ImGui::MenuItem("Auto Plot", nullptr, false, has_ulog))
            {
                auto_plot_ulog();
            }
            if (ImGui::IsItemHovered() && !has_ulog)
                ImGui::SetTooltip("Open a ULog file first");

            ImGui::Separator();
            const bool has_plots = auto_plot_active_ || plot_mgr_.field_count() > 0;
            if (ImGui::MenuItem("Close All Plots", nullptr, false, has_plots))
            {
                close_all_plots();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View"))
        {
            ImGui::MenuItem("ULog File", nullptr, &show_file_panel_);
            ImGui::MenuItem("Live Connection", nullptr, &show_live_panel_);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Connection"))
        {
            if (!bridge_.is_connected())
            {
                if (ImGui::MenuItem("Connect"))
                {
                    bridge_.init(cfg_.host, cfg_.port);
                    bridge_.start();
                    plot_mgr_.set_bridge(&bridge_);
                    show_live_panel_ = true;
                }
            }
            else
            {
                if (ImGui::MenuItem("Disconnect"))
                {
                    bridge_.shutdown();
                    plot_mgr_.set_bridge(nullptr);
                }
            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
#endif
}

// ---------------------------------------------------------------------------
// draw_status_bar
// ---------------------------------------------------------------------------

void Px4AppShell::draw_status_bar()
{
#ifdef SPECTRA_USE_IMGUI
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoSavedSettings |
                              ImGuiWindowFlags_NoTitleBar |
                              ImGuiWindowFlags_NoResize |
                              ImGuiWindowFlags_NoMove;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float bar_h = ImGui::GetFrameHeight();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x,
                                    viewport->WorkPos.y + viewport->WorkSize.y - bar_h));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, bar_h));

    if (ImGui::Begin("##StatusBar", nullptr, flags))
    {
        if (!last_open_error_.empty())
        {
            ImGui::Text("ULog open failed: %s", last_open_error_.c_str());
            ImGui::SameLine();
        }

        if (reader_.is_open())
        {
            ImGui::Text("ULog: %s | Duration: %.1fs | Topics: %zu | Messages: %zu",
                        reader_.metadata().path.c_str(),
                        reader_.metadata().duration_sec(),
                        reader_.topic_count(),
                        reader_.metadata().message_count);
        }

        if (bridge_.is_receiving())
        {
            if (reader_.is_open())
                ImGui::SameLine();
            ImGui::Text("Live: %s:%d (%.0f msg/s)",
                        bridge_.host().c_str(), bridge_.port(),
                        bridge_.message_rate());
        }

        if (reader_.is_open() || bridge_.is_receiving())
            ImGui::SameLine();
        ImGui::Text("Fields: %zu", plot_mgr_.field_count());
    }
    ImGui::End();
#endif
}

}   // namespace spectra::adapters::px4
