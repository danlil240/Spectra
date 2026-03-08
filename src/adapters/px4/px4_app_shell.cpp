#include "px4_app_shell.hpp"

#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>

#ifdef SPECTRA_USE_IMGUI
#include <imgui.h>
#include "../../../third_party/tinyfiledialogs.h"
#endif

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
    sync_canvas_figure(true);
    return true;
}

void Px4AppShell::sync_canvas_figure(bool force)
{
    if (!canvas_figure_)
        return;

    const uint64_t revision = plot_mgr_.revision();
    if (!force && revision == last_canvas_revision_)
        return;

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
