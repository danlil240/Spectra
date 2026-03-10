// ros2_ui_preview.cpp — Mock preview of the spectra-ros UI improvements.
//
// Renders all new ROS2 panel UI features using synthetic data, with no ROS2
// runtime required.  Demonstrates:
//   - Topic list with Hz sparklines + message-type colour coding
//   - Log viewer with per-severity pill toggles
//   - Diagnostics panel with stale elapsed-time badge
//   - Bag playback panel with event-marker ticks on scrub bar
//   - Subplot slot right-click context menu
//   - Expression editor autocomplete popup
//   - Scroll controller "paused — X.X s behind" status

#include <spectra/app.hpp>
#include <spectra/figure.hpp>

#include "ui/app/window_ui_context.hpp"
#include "ui/imgui/imgui_integration.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

// ============================================================================
// Synthetic data
// ============================================================================

struct MockTopic
{
    const char*           name;
    const char*           type;
    float                 hz;
    std::array<float, 30> hz_hist;
};

static MockTopic g_topics[] = {
    {"/imu", "sensor_msgs/msg/Imu", 100.0f, {}},
    {"/cmd_vel", "geometry_msgs/msg/Twist", 20.0f, {}},
    {"/scan", "sensor_msgs/msg/LaserScan", 10.0f, {}},
    {"/odom", "nav_msgs/msg/Odometry", 50.0f, {}},
    {"/diagnostics", "diagnostic_msgs/msg/DiagnosticArray", 1.0f, {}},
    {"/rosout", "rcl_interfaces/msg/Log", 5.0f, {}},
    {"/tf", "tf2_msgs/msg/TFMessage", 200.0f, {}},
    {"/camera/image", "sensor_msgs/msg/Image", 30.0f, {}},
};

static void init_hz_history()
{
    for (auto& t : g_topics)
    {
        for (int i = 0; i < 30; ++i)
        {
            const float noise = (float)(rand() % 20 - 10) / 100.0f;
            t.hz_hist[i]      = t.hz * (1.0f + noise);
        }
    }
}

// ============================================================================
// Helper: package-prefix colour (mirrors topic_list_panel.cpp)
// ============================================================================

static ImVec4 topic_type_color(const char* type_name)
{
    std::string s(type_name);
    auto        slash = s.find('/');
    std::string pkg   = (slash != std::string::npos) ? s.substr(0, slash) : s;

    if (pkg == "sensor_msgs")
        return {1.00f, 0.60f, 0.10f, 1.0f};
    if (pkg == "geometry_msgs")
        return {0.20f, 0.85f, 0.90f, 1.0f};
    if (pkg == "nav_msgs")
        return {0.75f, 0.40f, 0.90f, 1.0f};
    if (pkg == "diagnostic_msgs")
        return {0.95f, 0.90f, 0.15f, 1.0f};
    if (pkg == "std_msgs")
        return {0.40f, 0.70f, 1.00f, 1.0f};
    if (pkg == "tf2_msgs")
        return {0.20f, 0.90f, 0.70f, 1.0f};
    if (pkg == "rcl_interfaces")
        return {0.60f, 0.60f, 0.60f, 1.0f};
    return {0.75f, 0.75f, 0.75f, 1.0f};
}

// ============================================================================
// Helper: inline Hz sparkline
// ============================================================================

static void draw_hz_sparkline(const float* history, int n, float width, float height)
{
    if (n <= 0)
        return;
    ImVec2      p  = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    float max_val = 0.0f;
    for (int i = 0; i < n; ++i)
        if (history[i] > max_val)
            max_val = history[i];
    if (max_val <= 0.0f)
    {
        ImGui::Dummy({width, height});
        return;
    }

    const float inv  = 1.0f / max_val;
    const float step = (n > 1) ? width / (float)(n - 1) : width;
    for (int i = 1; i < n; ++i)
    {
        float x0 = p.x + (float)(i - 1) * step, x1 = p.x + (float)i * step;
        float y0 = p.y + height * (1.0f - history[i - 1] * inv);
        float y1 = p.y + height * (1.0f - history[i] * inv);
        dl->AddLine({x0, y0}, {x1, y1}, IM_COL32(80, 200, 120, 200), 1.0f);
    }
    ImGui::Dummy({width, height});
}

// ============================================================================
// Panel 1 — Topic List with sparklines + type colours
// ============================================================================

static void draw_topic_list_panel()
{
    ImGui::SetNextWindowSize({340, 420}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({10, 30}, ImGuiCond_FirstUseEver);
    ImGui::Begin("ROS2 Topics  [NEW: sparklines + type colours]");

    ImGui::InputText("##search", (char*)"", 0, ImGuiInputTextFlags_ReadOnly);
    ImGui::Separator();

    if (ImGui::BeginTable("topics",
                          3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          {0, 320}))
    {
        ImGui::TableSetupColumn("Topic", ImGuiTableColumnFlags_WidthStretch, 0.5f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 0.35f);
        ImGui::TableSetupColumn("Hz", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableHeadersRow();

        for (auto& t : g_topics)
        {
            ImGui::TableNextRow();

            // Topic name column
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(t.name);

            // Type column — coloured by package
            ImGui::TableSetColumnIndex(1);
            ImVec4 col = topic_type_color(t.type);
            ImGui::TextColored(col, "%s", t.type);

            // Hz + sparkline column
            ImGui::TableSetColumnIndex(2);
            char hz_buf[16];
            std::snprintf(hz_buf, sizeof(hz_buf), "%.0f", t.hz);
            ImGui::TextUnformatted(hz_buf);
            ImGui::SameLine(0.0f, 3.0f);
            const float sh = ImGui::GetTextLineHeight() * 0.8f;
            draw_hz_sparkline(t.hz_hist.data(), 30, 28.0f, sh);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

// ============================================================================
// Panel 2 — Log Viewer with pill toggles
// ============================================================================

static bool        g_pills[5]    = {true, true, true, true, true};   // DBG INFO WARN ERR FATAL
static const char* kPillLabels[] = {"DBG", "INFO", "WARN", "ERROR", "FATAL"};
static ImVec4      kPillColors[] = {
    {0.55f, 0.55f, 0.55f, 1.0f},
    {0.85f, 0.85f, 0.85f, 1.0f},
    {0.95f, 0.75f, 0.10f, 1.0f},
    {0.95f, 0.30f, 0.30f, 1.0f},
    {1.00f, 0.30f, 0.80f, 1.0f},
};

struct MockLogEntry
{
    int         sev;
    const char* node;
    const char* msg;
};
static MockLogEntry g_log[] = {
    {1, "/controller", "Controller started successfully"},
    {2, "/driver/motor", "Encoder timeout on joint 2"},
    {1, "/planner", "Path planning completed: 47 waypoints"},
    {3, "/sensor/lidar", "LiDAR scan rate below threshold"},
    {4, "/safety_monitor", "FATAL: Emergency stop triggered"},
    {1, "/localization", "EKF initialized with GPS prior"},
    {1, "/camera/driver", "Image pipeline ready at 30 Hz"},
    {2, "/battery_monitor", "Battery at 18% — consider charging"},
};

static void draw_log_viewer_panel()
{
    ImGui::SetNextWindowSize({600, 280}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({360, 30}, ImGuiCond_FirstUseEver);
    ImGui::Begin("ROS2 Log  [NEW: pill toggles + seek callback]");

    // Pill toggles (new UI)
    for (int i = 0; i < 5; ++i)
    {
        const ImVec4& col = kPillColors[i];
        ImVec4 btn_col    = g_pills[i] ? ImVec4(col.x * 0.45f, col.y * 0.45f, col.z * 0.45f, 0.9f)
                                       : ImVec4(0.18f, 0.18f, 0.18f, 0.7f);
        ImGui::PushStyleColor(ImGuiCol_Button, btn_col);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(col.x * 0.65f, col.y * 0.65f, col.z * 0.65f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, g_pills[i] ? col : ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
        char id[24];
        std::snprintf(id, sizeof(id), "%s##pill%d", kPillLabels[i], i);
        if (ImGui::SmallButton(id))
            g_pills[i] = !g_pills[i];
        ImGui::PopStyleColor(3);
        if (i < 4)
            ImGui::SameLine(0.0f, 3.0f);
    }
    ImGui::SameLine(0.0f, 12.0f);
    ImGui::SetNextItemWidth(150.0f);
    static char node_buf[64] = "";
    ImGui::InputText("Node##logfilt", node_buf, sizeof(node_buf));
    ImGui::SameLine();
    if (ImGui::SmallButton("X##clr"))
        node_buf[0] = '\0';
    ImGui::Separator();

    if (ImGui::BeginTable("logtbl",
                          3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          {0, 180}))
    {
        ImGui::TableSetupColumn("Sev", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Node", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Msg", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (auto& e : g_log)
        {
            if (!g_pills[e.sev])
                continue;   // hidden by pill toggle
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(kPillColors[e.sev], "%s", kPillLabels[e.sev]);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(e.node);
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(e.msg);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

// ============================================================================
// Panel 3 — Diagnostics with stale elapsed badge
// ============================================================================

struct MockDiagComp
{
    const char* name;
    int         level;
    float       stale_sec;
};
static MockDiagComp g_diag[] = {
    {"battery", 0, 0.0f},
    {"motor/joint1", 0, 0.0f},
    {"motor/joint2", 1, 0.0f},
    {"lidar/driver", 3, 12.4f},   // STALE 12.4 s
    {"camera", 2, 0.0f},
    {"gps", 3, 87.2f},   // STALE 87.2 s
    {"imu/bno055", 0, 0.0f},
};

static const ImVec4 kDiagColors[] = {
    {0.20f, 0.80f, 0.30f, 1.0f},   // OK   — green
    {0.95f, 0.75f, 0.10f, 1.0f},   // WARN — yellow
    {0.95f, 0.30f, 0.30f, 1.0f},   // ERR  — red
    {0.55f, 0.55f, 0.55f, 0.8f},   // STALE— gray
};
static const char* kDiagLabels[] = {"OK", "WARN", "ERR", "STALE"};

static void draw_diagnostics_panel()
{
    ImGui::SetNextWindowSize({400, 300}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({10, 470}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Diagnostics  [NEW: stale elapsed badge]");

    // Summary badges
    int counts[4] = {};
    for (auto& c : g_diag)
        if (c.level >= 0 && c.level < 4)
            counts[c.level]++;
    for (int i = 0; i < 4; ++i)
    {
        ImGui::TextColored(kDiagColors[i], "%s: %d", kDiagLabels[i], counts[i]);
        if (i < 3)
            ImGui::SameLine(0.0f, 16.0f);
    }
    ImGui::Separator();

    if (ImGui::BeginTable("diagtbl",
                          3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          {0, 200}))
    {
        ImGui::TableSetupColumn("Component", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Elapsed", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();

        for (auto& c : g_diag)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(c.name);

            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(kDiagColors[c.level], "%s", kDiagLabels[c.level]);

            // Stale elapsed-time badge (NEW)
            ImGui::TableSetColumnIndex(2);
            if (c.level == 3 && c.stale_sec > 0.5f)
            {
                char buf[32];
                if (c.stale_sec < 60.0f)
                    std::snprintf(buf, sizeof(buf), "%.0f s", c.stale_sec);
                else
                    std::snprintf(buf, sizeof(buf), "%.1f min", c.stale_sec / 60.0f);
                ImGui::TextColored({0.80f, 0.55f, 0.20f, 1.0f}, "%s", buf);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Last update %.1f s ago", c.stale_sec);
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

// ============================================================================
// Panel 4 — Bag Playback with event-marker ticks
// ============================================================================

struct MockEvent
{
    float                pos;
    std::array<float, 4> color;
    const char*          tooltip;
};
static MockEvent g_events[] = {
    {0.12f, {1.0f, 0.4f, 0.1f, 0.8f}, "Motor fault detected"},
    {0.35f, {0.3f, 0.9f, 1.0f, 0.8f}, "Waypoint 1 reached"},
    {0.51f, {1.0f, 0.9f, 0.1f, 0.8f}, "Battery warning"},
    {0.74f, {0.3f, 0.9f, 1.0f, 0.8f}, "Waypoint 2 reached"},
    {0.91f, {0.9f, 0.2f, 0.2f, 0.9f}, "Emergency stop"},
};

static float g_playhead = 0.38f;

static void draw_bag_playback_panel()
{
    ImGui::SetNextWindowSize({620, 110}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({420, 470}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Bag Playback  [NEW: event-marker ticks]");

    // Transport buttons
    ImGui::SmallButton("  |<  ");
    ImGui::SameLine();
    ImGui::SmallButton("  > ");
    ImGui::SameLine();
    ImGui::SmallButton("  []  ");
    ImGui::SameLine();
    ImGui::SmallButton("  <  ");
    ImGui::SameLine();
    ImGui::SmallButton("  >  ");
    ImGui::SameLine();
    ImGui::Text("  %.1f / 120.3 s   1.0x", g_playhead * 120.3f);

    // Progress bar with event marker ticks
    ImVec2      bar_min = ImGui::GetCursorScreenPos();
    float       bar_w   = ImGui::GetContentRegionAvail().x;
    float       bar_h   = 14.0f;
    ImVec2      bar_max = {bar_min.x + bar_w, bar_min.y + bar_h};
    ImDrawList* dl      = ImGui::GetWindowDrawList();

    // Background
    dl->AddRectFilled(bar_min, bar_max, IM_COL32(40, 40, 50, 255), 3.0f);

    // Filled portion
    float fill_x = bar_min.x + bar_w * g_playhead;
    dl->AddRectFilled(bar_min, {fill_x, bar_max.y}, IM_COL32(70, 140, 220, 200), 3.0f);

    // Event marker ticks (NEW)
    const float tick_h = bar_h * 0.85f;
    ImVec2      mouse  = ImGui::GetMousePos();
    for (auto& ev : g_events)
    {
        float tx  = bar_min.x + bar_w * ev.pos;
        ImU32 col = IM_COL32((int)(ev.color[0] * 255),
                             (int)(ev.color[1] * 255),
                             (int)(ev.color[2] * 255),
                             (int)(ev.color[3] * 255));
        dl->AddLine({tx, bar_min.y}, {tx, bar_min.y + tick_h}, col, 2.0f);

        if (std::abs(mouse.x - tx) < 5.0f && mouse.y >= bar_min.y && mouse.y <= bar_max.y)
            ImGui::SetTooltip("%s", ev.tooltip);
    }

    // Playhead cursor
    dl->AddLine({fill_x, bar_min.y - 2},
                {fill_x, bar_max.y + 2},
                IM_COL32(255, 255, 255, 200),
                1.5f);

    // Drag to seek
    ImGui::InvisibleButton("##scrub", {bar_w, bar_h});
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(0))
    {
        float rel  = (mouse.x - bar_min.x) / bar_w;
        g_playhead = std::clamp(rel, 0.0f, 1.0f);
    }

    ImGui::End();
}

// ============================================================================
// Panel 5 — Scroll controller status
// ============================================================================

static void draw_scroll_status_panel()
{
    ImGui::SetNextWindowSize({360, 130}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({970, 30}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Scroll Status  [NEW: paused — X s behind]");

    static bool  paused   = true;
    static float behind_s = 4.2f;

    if (paused)
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "paused \xe2\x80\x94 %.1f s behind", behind_s);
        ImGui::TextColored({0.95f, 0.75f, 0.20f, 1.0f}, "%s", buf);
    }
    else
    {
        ImGui::TextColored({0.30f, 0.90f, 0.30f, 1.0f}, "following");
    }

    ImGui::SliderFloat("Behind", &behind_s, 0.0f, 60.0f);
    if (ImGui::Button(paused ? "Resume" : "Pause"))
        paused = !paused;

    ImGui::End();
}

// ============================================================================
// Panel 6 — Subplot slot context menu
// ============================================================================

static void draw_subplot_context_menu_panel()
{
    ImGui::SetNextWindowSize({360, 220}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({970, 180}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Subplot Slots  [NEW: right-click context menu]");

    ImGui::TextDisabled("Right-click a slot to see the context menu:");
    ImGui::Spacing();

    static const char* slot_topics[] = {
        "/imu/linear_acceleration.x",
        "/imu/linear_acceleration.y",
        "/odom/twist.linear.x",
    };
    static float slot_tw[] = {-1.0f, 30.0f, -1.0f};

    for (int s = 0; s < 3; ++s)
    {
        char label[64];
        std::snprintf(label, sizeof(label), "Slot %d: %s", s + 1, slot_topics[s]);
        ImGui::PushStyleColor(ImGuiCol_Button, {0.15f, 0.18f, 0.25f, 1.0f});
        ImGui::Button(label, {ImGui::GetContentRegionAvail().x, 30.0f});
        ImGui::PopStyleColor();

        char popup_id[32];
        std::snprintf(popup_id, sizeof(popup_id), "##slotmenu%d", s);
        if (ImGui::BeginPopupContextItem(popup_id))
        {
            ImGui::TextDisabled("Slot %d", s + 1);
            ImGui::Separator();
            if (ImGui::MenuItem("Clear"))
            {
            }
            if (ImGui::MenuItem("Duplicate"))
            {
            }
            if (ImGui::MenuItem("Detach"))
            {
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Time window"))
            {
                const float presets[] = {5, 10, 30, 60, 300, 600};
                const char* labels[]  = {"5 s", "10 s", "30 s", "1 min", "5 min", "10 min"};
                for (int i = 0; i < 6; ++i)
                {
                    bool sel = (slot_tw[s] == presets[i]);
                    if (ImGui::MenuItem(labels[i], nullptr, sel))
                        slot_tw[s] = presets[i];
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Reset to global", nullptr, slot_tw[s] < 0))
                    slot_tw[s] = -1.0f;
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }

        if (slot_tw[s] > 0.0f)
        {
            ImGui::SameLine();
            ImGui::TextDisabled(" [%.0fs window]", slot_tw[s]);
        }
    }
    ImGui::End();
}

// ============================================================================
// Panel 7 — Expression editor autocomplete
// ============================================================================

static const char* kFieldEntries[] = {
    "$imu/linear_acceleration.x",
    "$imu/linear_acceleration.y",
    "$imu/linear_acceleration.z",
    "$imu/angular_velocity.x",
    "$odom/twist.linear.x",
    "$odom/pose.pose.position.x",
    "$cmd_vel/linear.x",
    "$cmd_vel/angular.z",
};

static void draw_expression_editor_panel()
{
    ImGui::SetNextWindowSize({460, 200}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({10, 320}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Expression Editor  [NEW: $-field autocomplete]");

    static char expr_buf[256] = "sqrt($imu/linear_ac";
    static bool ac_open       = true;
    static int  ac_sel        = 0;

    ImGui::TextDisabled("Expression:");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##expr", expr_buf, sizeof(expr_buf)))
    {
        // Show autocomplete when buffer ends with a $ prefix
        const char* dollar = strrchr(expr_buf, '$');
        ac_open            = (dollar != nullptr);
    }

    // Autocomplete popup (NEW)
    if (ac_open)
    {
        const char* dollar = strrchr(expr_buf, '$');
        const char* prefix = dollar ? dollar : "";

        ImGui::SetNextWindowPos({ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 2.0f});
        ImGui::SetNextWindowSize({400, 160});
        ImGui::SetNextWindowBgAlpha(0.95f);
        if (ImGui::Begin("##acpopup",
                         nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                             | ImGuiWindowFlags_NoSavedSettings
                             | ImGuiWindowFlags_NoFocusOnAppearing))
        {
            int visible = 0;
            for (int i = 0; i < (int)(sizeof(kFieldEntries) / sizeof(*kFieldEntries)); ++i)
            {
                if (strstr(kFieldEntries[i], prefix + 1) == nullptr)
                    continue;
                bool sel = (visible == ac_sel);
                if (sel)
                    ImGui::PushStyleColor(ImGuiCol_Text, {0.3f, 0.9f, 1.0f, 1.0f});
                ImGui::Text("%s  %s", sel ? ">" : " ", kFieldEntries[i]);
                if (sel)
                    ImGui::PopStyleColor();
                if (ImGui::IsItemClicked())
                {
                    // Insert completion
                    std::snprintf(expr_buf, sizeof(expr_buf), "sqrt(%s", kFieldEntries[i]);
                    ac_open = false;
                }
                ++visible;
            }
            if (visible == 0)
            {
                ImGui::TextDisabled("(no matches)");
                ac_open = false;
            }
        }
        ImGui::End();
    }

    ImGui::Spacing();
    ImGui::SmallButton("Apply  (Ctrl+Enter)");
    ImGui::SameLine();
    if (ImGui::SmallButton("Close autocomplete"))
        ac_open = !ac_open;

    ImGui::End();
}

// ============================================================================
// Main draw
// ============================================================================

#ifdef SPECTRA_USE_IMGUI
static void draw_all()
{
    draw_topic_list_panel();
    draw_log_viewer_panel();
    draw_diagnostics_panel();
    draw_bag_playback_panel();
    draw_scroll_status_panel();
    draw_subplot_context_menu_panel();
    draw_expression_editor_panel();
}
#endif

// ============================================================================
// Entry point
// ============================================================================

int main()
{
    init_hz_history();

    spectra::AppConfig cfg;
    spectra::App       app(cfg);

    spectra::FigureConfig fig_cfg;
    fig_cfg.width  = 1400;
    fig_cfg.height = 860;
    auto& fig      = app.figure(fig_cfg);
    fig.subplot(1, 1, 1);

    app.init_runtime();

#ifdef SPECTRA_USE_IMGUI
    auto* ui_ctx = app.ui_context();
    if (ui_ctx && ui_ctx->imgui_ui)
        ui_ctx->imgui_ui->set_extra_draw_callback(draw_all);
#endif

    // Animate at 60 fps to keep the window live.
    fig.animate().fps(60.0f).on_frame([](spectra::Frame&) {}).loop(true).play();

    // Drive the render loop manually so the extra_draw_callback fires each frame.
    while (true)
    {
        auto result = app.step();
        if (result.should_exit)
            break;
    }
    app.shutdown_runtime();
    return 0;
}
