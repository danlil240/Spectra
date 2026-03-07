#include "ui/topic_list_panel.hpp"
#include "ui/field_drag_drop.hpp"
#include "ui/topic_echo_panel.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#ifdef SPECTRA_USE_IMGUI
#include <imgui.h>

#include "ui/theme/icons.hpp"
#endif

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static int64_t wall_clock_ns()
{
    using namespace std::chrono;
    return duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
}

static int displayed_endpoint_count(int total_count, int local_count)
{
    return std::max(0, total_count - local_count);
}

// ---------------------------------------------------------------------------
// TopicStats
// ---------------------------------------------------------------------------

void TopicStats::push(int64_t now_ns, size_t bytes)
{
    timestamps.push_back(now_ns);
    sizes.push_back(bytes);
    last_msg_ns = now_ns;
    ++total_messages;
    total_bytes += bytes;
}

void TopicStats::prune_and_compute(int64_t now_ns, int64_t window_ns)
{
    const int64_t cutoff = now_ns - window_ns;

    // Remove entries older than the window.
    while (!timestamps.empty() && timestamps.front() < cutoff) {
        timestamps.pop_front();
        sizes.pop_front();
    }

    const size_t n = timestamps.size();
    if (n < 2) {
        hz           = (n == 1) ? 1.0 : 0.0;
        bandwidth_bps = 0.0;
    } else {
        const double span_s = static_cast<double>(timestamps.back() - timestamps.front()) * 1e-9;
        hz = (span_s > 0.0) ? static_cast<double>(n - 1) / span_s : 0.0;

        // BW = total bytes in window / window size in seconds.
        size_t total = 0;
        for (auto s : sizes) total += s;
        const double win_s = static_cast<double>(window_ns) * 1e-9;
        bandwidth_bps = static_cast<double>(total) / win_s;
    }

    // EMA smoothing for displayed Hz — alpha chosen so the display
    // settles in ~2-3 seconds (lower alpha = smoother, slower response).
    constexpr double EMA_ALPHA = 0.15;
    if (displayed_hz <= 0.0 && hz > 0.0)
        displayed_hz = hz;   // initialise on first sample
    else
        displayed_hz = EMA_ALPHA * hz + (1.0 - EMA_ALPHA) * displayed_hz;
    // Snap to zero when raw Hz drops below a tiny threshold.
    if (hz <= 0.0)
        displayed_hz *= (1.0 - EMA_ALPHA);  // decay towards zero
    if (displayed_hz < 0.05)
        displayed_hz = 0.0;

    // Adaptive stale threshold: for low-frequency topics (≤ 2 Hz) give
    // at least 3× the expected period so the dot doesn't flicker.
    int64_t stale_ns = 2'000'000'000LL;  // 2 s default
    if (displayed_hz > 0.0 && displayed_hz <= 2.0)
    {
        const int64_t period_ns = static_cast<int64_t>(1.0e9 / displayed_hz);
        stale_ns = std::max(stale_ns, period_ns * 3);
    }
    active = (last_msg_ns > 0) && (now_ns - last_msg_ns < stale_ns);

    // Sample Hz history once per second for sparkline.
    const int64_t sample_interval_ns = 1'000'000'000LL;
    if (last_hz_sample_ns == 0 || (now_ns - last_hz_sample_ns) >= sample_interval_ns) {
        hz_history.push_back(static_cast<float>(hz));
        if (hz_history.size() > HZ_HISTORY_LEN)
            hz_history.pop_front();
        last_hz_sample_ns = now_ns;
    }
}

// ---------------------------------------------------------------------------
// TopicListPanel — construction
// ---------------------------------------------------------------------------

TopicListPanel::TopicListPanel()
{
    std::memset(filter_buf_, 0, sizeof(filter_buf_));
}

TopicListPanel::~TopicListPanel() = default;

void TopicListPanel::set_column_visibility(const ColumnVisibility& visibility)
{
    col_show_type_ = visibility.show_type;
    col_show_hz_ = visibility.show_hz;
    col_show_pubs_ = visibility.show_pubs;
    col_show_subs_ = visibility.show_subs;
    col_show_bw_ = visibility.show_bw;
}

TopicListPanel::ColumnVisibility TopicListPanel::column_visibility() const
{
    return {
        .show_type = col_show_type_,
        .show_hz = col_show_hz_,
        .show_pubs = col_show_pubs_,
        .show_subs = col_show_subs_,
        .show_bw = col_show_bw_,
    };
}

// ---------------------------------------------------------------------------
// Wiring
// ---------------------------------------------------------------------------

void TopicListPanel::set_topic_discovery(TopicDiscovery* disc)
{
    disc_ = disc;
}

// ---------------------------------------------------------------------------
// Statistics injection (executor thread)
// ---------------------------------------------------------------------------

void TopicListPanel::notify_message(const std::string& topic_name, size_t bytes)
{
    const int64_t now = wall_clock_ns();
    std::lock_guard<std::mutex> lk(stats_mutex_);
    stats_map_[topic_name].push(now, bytes);
}

// ---------------------------------------------------------------------------
// Testing helpers
// ---------------------------------------------------------------------------

void TopicListPanel::set_topics(const std::vector<TopicInfo>& topics)
{
    std::lock_guard<std::mutex> lk(topics_mutex_);
    topics_ = topics;
    rebuild_tree();
    filter_dirty_ = true;
}

void TopicListPanel::set_filter(const std::string& f)
{
    filter_str_ = f;
    const size_t copy_len = std::min(f.size(), sizeof(filter_buf_) - 1);
    std::memcpy(filter_buf_, f.c_str(), copy_len);
    filter_buf_[copy_len] = '\0';
    filter_dirty_ = true;
}

TopicListPanel::StatsSnapshot TopicListPanel::stats_for(const std::string& topic_name) const
{
    const int64_t now = wall_clock_ns();
    std::lock_guard<std::mutex> lk(stats_mutex_);
    auto it = stats_map_.find(topic_name);
    if (it == stats_map_.end()) {
        return {0.0, 0.0, false, 0};
    }
    // Compute on a copy so we don't mutate under const.
    TopicStats copy = it->second;
    copy.prune_and_compute(now, static_cast<int64_t>(stats_window_ms_) * 1'000'000LL);
    return {copy.hz, copy.bandwidth_bps, copy.active, copy.total_messages};
}

size_t TopicListPanel::topic_count() const
{
    std::lock_guard<std::mutex> lk(topics_mutex_);
    return topics_.size();
}

size_t TopicListPanel::filtered_topic_count() const
{
    // Rebuild filtered list if dirty.
    if (filter_dirty_) {
        filtered_names_.clear();
        std::lock_guard<std::mutex> lk(topics_mutex_);
        for (const auto& t : topics_) {
            if (filter_str_.empty() ||
                t.name.find(filter_str_) != std::string::npos) {
                filtered_names_.push_back(t.name);
            }
        }
        filter_dirty_ = false;
    }
    return filtered_names_.size();
}

// ---------------------------------------------------------------------------
// Tree building
// ---------------------------------------------------------------------------

/*static*/ std::pair<std::string, std::string>
TopicListPanel::split_namespace(const std::string& topic_name)
{
    // Find last '/' to split leaf from namespace.
    const auto pos = topic_name.rfind('/');
    if (pos == std::string::npos || pos == 0) {
        // No namespace or root-level topic.
        return {"/", topic_name.substr(pos == std::string::npos ? 0 : 1)};
    }
    return {topic_name.substr(0, pos), topic_name.substr(pos + 1)};
}

void TopicListPanel::rebuild_tree()
{
    // Assumes topics_mutex_ is held by caller.
    ns_tree_.clear();
    root_namespaces_.clear();
    root_topics_.clear();

    for (const auto& t : topics_) {
        auto [ns, leaf] = split_namespace(t.name);

        if (ns == "/") {
            root_topics_.push_back(t.name);
            continue;
        }

        // Ensure the namespace node exists.
        if (ns_tree_.find(ns) == ns_tree_.end()) {
            NamespaceNode node;
            node.ns = ns;
            // label = last segment of ns.
            const auto slash = ns.rfind('/');
            node.label = (slash == std::string::npos)
                             ? ns
                             : ns.substr(slash + 1);
            ns_tree_[ns] = std::move(node);
        }
        ns_tree_[ns].topic_names.push_back(t.name);

        // Register parent relationship walking up.
        std::string child_ns = ns;
        while (true) {
            const auto parent_slash = child_ns.rfind('/');
            if (parent_slash == std::string::npos || parent_slash == 0) {
                // child_ns is a root-level namespace.
                auto& root_vec = root_namespaces_;
                if (std::find(root_vec.begin(), root_vec.end(), child_ns) == root_vec.end()) {
                    root_vec.push_back(child_ns);
                }
                break;
            }
            const std::string parent_ns = child_ns.substr(0, parent_slash);
            if (ns_tree_.find(parent_ns) == ns_tree_.end()) {
                NamespaceNode pnode;
                pnode.ns    = parent_ns;
                const auto s = parent_ns.rfind('/');
                pnode.label = (s == std::string::npos)
                                  ? parent_ns
                                  : parent_ns.substr(s + 1);
                ns_tree_[parent_ns] = std::move(pnode);
            }
            auto& children = ns_tree_[parent_ns].children;
            if (std::find(children.begin(), children.end(), child_ns) == children.end()) {
                children.push_back(child_ns);
            }
            child_ns = parent_ns;
        }
    }

    // Deduplicate root_namespaces_.
    std::sort(root_namespaces_.begin(), root_namespaces_.end());
    root_namespaces_.erase(
        std::unique(root_namespaces_.begin(), root_namespaces_.end()),
        root_namespaces_.end());
    std::sort(root_topics_.begin(), root_topics_.end());
}

// ---------------------------------------------------------------------------
// Formatting helpers
// ---------------------------------------------------------------------------

/*static*/ std::string TopicListPanel::format_hz(double hz)
{
    if (hz <= 0.0) return "-";
    char buf[32];
    if (hz >= 1000.0)
        std::snprintf(buf, sizeof(buf), "%.0f", hz);
    else
        std::snprintf(buf, sizeof(buf), "%.2f", hz);
    return buf;
}

/*static*/ std::string TopicListPanel::format_bw(double bps)
{
    if (bps <= 0.0) return "-";
    char buf[32];
    if (bps >= 1024.0 * 1024.0)
        std::snprintf(buf, sizeof(buf), "%.1f MB/s", bps / (1024.0 * 1024.0));
    else if (bps >= 1024.0)
        std::snprintf(buf, sizeof(buf), "%.1f KB/s", bps / 1024.0);
    else
        std::snprintf(buf, sizeof(buf), "%.0f B/s", bps);
    return buf;
}

/*static*/ int64_t TopicListPanel::now_ns()
{
    return wall_clock_ns();
}

// ---------------------------------------------------------------------------
// ImGui rendering
// ---------------------------------------------------------------------------

#ifdef SPECTRA_USE_IMGUI

// Color constants.
static constexpr ImVec4 kColorActive   = {0.20f, 0.80f, 0.30f, 1.0f};  // green
static constexpr ImVec4 kColorStale    = {0.50f, 0.50f, 0.50f, 1.0f};  // gray
static constexpr ImVec4 kColorSelected = {0.26f, 0.59f, 0.98f, 0.25f}; // blue tint

// Echo field colors (matching TopicEchoPanel for visual consistency).
static constexpr ImVec4 kEchoNumeric = {0.60f, 0.88f, 1.00f, 1.0f};  // light blue
static constexpr ImVec4 kEchoText    = {0.90f, 0.90f, 0.65f, 1.0f};  // light yellow
static constexpr ImVec4 kEchoNested  = {0.80f, 0.70f, 0.50f, 1.0f};  // tan
static constexpr ImVec4 kEchoArray   = {0.75f, 0.60f, 0.90f, 1.0f};  // lavender

// Returns a distinguishing color for a ROS2 message type package prefix.
// e.g. "sensor_msgs/msg/Imu" → sensor category color.
static ImVec4 topic_type_color(const std::string& type_name)
{
    // Match on the package name prefix before "/msg/".
    auto slash = type_name.find('/');
    const std::string pkg = (slash != std::string::npos) ? type_name.substr(0, slash) : type_name;

    // Sensor data — orange
    if (pkg == "sensor_msgs")   return {1.00f, 0.60f, 0.10f, 1.0f};
    // Geometry / transforms — cyan
    if (pkg == "geometry_msgs") return {0.20f, 0.85f, 0.90f, 1.0f};
    // Navigation — purple
    if (pkg == "nav_msgs")      return {0.75f, 0.40f, 0.90f, 1.0f};
    // Diagnostics — yellow
    if (pkg == "diagnostic_msgs") return {0.95f, 0.90f, 0.15f, 1.0f};
    // Std messages — light blue
    if (pkg == "std_msgs")      return {0.40f, 0.70f, 1.00f, 1.0f};
    // Actions / lifecycle — pink
    if (pkg == "action_msgs" || pkg == "lifecycle_msgs")
                                return {1.00f, 0.45f, 0.70f, 1.0f};
    // TF — teal
    if (pkg == "tf2_msgs")      return {0.20f, 0.90f, 0.70f, 1.0f};
    // Default — neutral gray-white
    return {0.75f, 0.75f, 0.75f, 1.0f};
}

// Draw a small inline sparkline using ImDrawList.
// Renders in the current cursor position using width × height pixels.
static void draw_hz_sparkline(const std::deque<float>& history,
                               float width, float height)
{
    if (history.empty()) return;

    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    float max_val = 0.0f;
    for (float v : history) if (v > max_val) max_val = v;
    if (max_val <= 0.0f) {
        // No data — draw a flat baseline.
        const float y = p.y + height;
        dl->AddLine({p.x, y}, {p.x + width, y},
                    IM_COL32(80, 80, 80, 180), 1.0f);
        ImGui::Dummy({width, height});
        return;
    }

    const float inv_max = 1.0f / max_val;
    const int n = static_cast<int>(history.size());
    const float step = (n > 1) ? width / static_cast<float>(n - 1) : width;

    for (int i = 1; i < n; ++i) {
        const float x0 = p.x + static_cast<float>(i - 1) * step;
        const float x1 = p.x + static_cast<float>(i)     * step;
        const float y0 = p.y + height * (1.0f - history[i - 1] * inv_max);
        const float y1 = p.y + height * (1.0f - history[i]     * inv_max);
        dl->AddLine({x0, y0}, {x1, y1}, IM_COL32(80, 200, 120, 200), 1.0f);
    }
    ImGui::Dummy({width, height});
}

void TopicListPanel::draw(bool* p_open)
{
    // Fetch discovery data OUTSIDE any lock to avoid ABBA with
    // TopicDiscovery::mutex_.
    std::vector<TopicInfo> fresh;
    bool have_fresh = false;
    if (disc_) {
        fresh = disc_->topics();
        have_fresh = true;
    }

    // Prune and compute stats under stats_mutex_ once per frame.
    // This lock is scoped — released before topics_mutex_ is acquired.
    const int64_t cur_ns = wall_clock_ns();
    const int64_t win_ns = static_cast<int64_t>(stats_window_ms_) * 1'000'000LL;
    {
        std::lock_guard<std::mutex> lk(stats_mutex_);
        for (auto& [name, st] : stats_map_) {
            st.prune_and_compute(cur_ns, win_ns);
        }
    }

    // Single topics_mutex_ lock: covers discovery update + rendering.
    std::lock_guard<std::mutex> lk_t(topics_mutex_);

    // Apply discovery update (still under topics_mutex_).
    if (have_fresh) {
        if (fresh.size() != topics_.size()) {
            topics_ = std::move(fresh);
            rebuild_tree();
            filter_dirty_ = true;
        } else {
            bool changed = false;
            for (size_t i = 0; i < topics_.size(); ++i) {
                if (topics_[i].name != fresh[i].name) { changed = true; break; }
            }
            if (changed) {
                topics_ = std::move(fresh);
                rebuild_tree();
                filter_dirty_ = true;
            }
        }
    }

    // --- Window ---
    if (!ImGui::GetCurrentContext()) return;
    ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (!ImGui::Begin(title_.c_str(), p_open, flags)) {
        ImGui::End();
        return;
    }

    // --- Search bar ---
    ImGui::SetNextItemWidth(-120.0f);
    const bool filter_changed =
        ImGui::InputTextWithHint("##filter", "Search topics...", filter_buf_, sizeof(filter_buf_));
    if (filter_changed) {
        filter_str_   = filter_buf_;
        filter_dirty_ = true;
    }
    ImGui::SameLine();

    // Column visibility settings gear.
    if (ImGui::SmallButton(ui::icon_str(ui::Icon::Settings)))
        ImGui::OpenPopup("##col_vis");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Column visibility");
    if (ImGui::BeginPopup("##col_vis"))
    {
        ImGui::TextDisabled("Visible columns");
        ImGui::Separator();
        ImGui::Checkbox("Type", &col_show_type_);
        ImGui::Checkbox("Hz",   &col_show_hz_);
        ImGui::Checkbox("Pubs", &col_show_pubs_);
        ImGui::Checkbox("Subs", &col_show_subs_);
        ImGui::Checkbox("BW",   &col_show_bw_);
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu topic%s",
                        topics_.size(),
                        topics_.size() == 1 ? "" : "s");

    ImGui::Separator();

    // --- Column table ---
    constexpr ImGuiTableFlags kTableFlags =
        ImGuiTableFlags_Resizable      |
        ImGuiTableFlags_BordersInnerV  |
        ImGuiTableFlags_ScrollY        |
        ImGuiTableFlags_RowBg          |
        ImGuiTableFlags_SizingStretchProp;

    const float footer_h  = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
    const ImVec2 table_sz = {0.0f, ImGui::GetContentRegionAvail().y - footer_h};

    // Count visible optional columns: always Topic + optionally Type/Hz/Pubs/Subs/BW.
    const int n_cols = 1
        + (col_show_type_ ? 1 : 0)
        + (col_show_hz_   ? 1 : 0)
        + (col_show_pubs_ ? 1 : 0)
        + (col_show_subs_ ? 1 : 0)
        + (col_show_bw_   ? 1 : 0);

    if (!ImGui::BeginTable("##topics", n_cols, kTableFlags, table_sz)) {
        ImGui::End();
        return;
    }

    ImGui::TableSetupScrollFreeze(0, 1);  // freeze header row
    ImGui::TableSetupColumn("Topic",    ImGuiTableColumnFlags_WidthStretch, 3.0f);
    if (col_show_type_) ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 2.5f);
    if (col_show_hz_)   ImGui::TableSetupColumn("Hz",   ImGuiTableColumnFlags_WidthFixed,   60.0f);
    if (col_show_pubs_) ImGui::TableSetupColumn("Pubs", ImGuiTableColumnFlags_WidthFixed,   38.0f);
    if (col_show_subs_) ImGui::TableSetupColumn("Subs", ImGuiTableColumnFlags_WidthFixed,   38.0f);
    if (col_show_bw_)   ImGui::TableSetupColumn("BW",   ImGuiTableColumnFlags_WidthFixed,   72.0f);
    ImGui::TableHeadersRow();

    // --- Rows (already holding topics_mutex_) ---
    if (group_by_namespace_) {
        // Root-level loose topics first (e.g. /rosout at "/").
        for (const auto& tname : root_topics_) {
            if (!filter_str_.empty() && tname.find(filter_str_) == std::string::npos)
                continue;
            auto it = std::find_if(topics_.begin(), topics_.end(),
                                   [&](const TopicInfo& ti) { return ti.name == tname; });
            if (it == topics_.end()) continue;
            {
                std::lock_guard<std::mutex> lk_s(stats_mutex_);
                (void)stats_map_[tname];  // ensure entry exists
            }
            std::lock_guard<std::mutex> lk_s(stats_mutex_);
            draw_topic_row(*it, stats_map_[tname]);
        }
        // Namespace groups.
        for (const auto& ns : root_namespaces_) {
            draw_namespace_node(ns, 0);
        }
    } else {
        // Flat list, sorted by name.
        std::vector<const TopicInfo*> sorted;
        sorted.reserve(topics_.size());
        for (const auto& t : topics_) {
            if (filter_str_.empty() || t.name.find(filter_str_) != std::string::npos)
                sorted.push_back(&t);
        }
        std::sort(sorted.begin(), sorted.end(),
                  [](const TopicInfo* a, const TopicInfo* b) { return a->name < b->name; });

        std::lock_guard<std::mutex> lk_s(stats_mutex_);
        for (const auto* t : sorted) {
            draw_topic_row(*t, stats_map_[t->name]);
        }
    }

    ImGui::EndTable();

    // --- Status bar ---
    {
        std::lock_guard<std::mutex> lk_s(stats_mutex_);
        size_t active_count = 0;
        for (const auto& [name, st] : stats_map_) {
            if (st.active) ++active_count;
        }
        ImGui::TextDisabled("Active: %zu / %zu",
                            active_count, topics_.size());
    }

    ImGui::End();
}

void TopicListPanel::draw_namespace_node(const std::string& ns, int /*depth*/)
{
    auto it = ns_tree_.find(ns);
    if (it == ns_tree_.end()) return;

    NamespaceNode& node = it->second;

    // Check if any topic in this subtree passes the filter.
    bool any_visible = false;
    if (filter_str_.empty()) {
        any_visible = !node.topic_names.empty() || !node.children.empty();
    } else {
        for (const auto& tname : node.topic_names) {
            if (tname.find(filter_str_) != std::string::npos) {
                any_visible = true;
                break;
            }
        }
        if (!any_visible) {
            for (const auto& child : node.children) {
                // Recurse to check.
                auto cit = ns_tree_.find(child);
                if (cit != ns_tree_.end()) {
                    for (const auto& tname : cit->second.topic_names) {
                        if (tname.find(filter_str_) != std::string::npos) {
                            any_visible = true;
                            break;
                        }
                    }
                }
                if (any_visible) break;
            }
        }
    }
    if (!any_visible) return;

    // Namespace header row spans all columns.
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);

    // Use SPECTRA_USE_IMGUI tree node.
    const ImGuiTreeNodeFlags tree_flags =
        ImGuiTreeNodeFlags_SpanFullWidth |
        ImGuiTreeNodeFlags_DefaultOpen;

    const bool open = ImGui::TreeNodeEx(node.label.c_str(), tree_flags);

    if (open) {
        // Leaf topics — lock stats_mutex_ only for this section.
        // Must release before recursing into child namespaces, because
        // std::mutex is non-recursive and the recursive call also locks it.
        {
            std::lock_guard<std::mutex> lk_s(stats_mutex_);
            for (const auto& tname : node.topic_names) {
                if (!filter_str_.empty() && tname.find(filter_str_) == std::string::npos)
                    continue;
                auto tit = std::find_if(topics_.begin(), topics_.end(),
                                        [&](const TopicInfo& ti) { return ti.name == tname; });
                if (tit == topics_.end()) continue;
                draw_topic_row(*tit, stats_map_[tname]);
            }
        }

        // Child namespaces (recursive — stats_mutex_ must NOT be held here).
        std::vector<std::string> sorted_children = node.children;
        std::sort(sorted_children.begin(), sorted_children.end());
        for (const auto& child : sorted_children) {
            draw_namespace_node(child, 0);
        }

        ImGui::TreePop();
    }
}

void TopicListPanel::draw_topic_row(const TopicInfo& info, TopicStats& stats)
{
    ImGui::TableNextRow();

    // Column 0: Name
    ImGui::TableSetColumnIndex(0);

    const bool is_selected = (info.name == selected_topic_);
    const bool is_expanded = is_topic_expanded(info.name);
    if (is_selected) {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                               ImGui::ColorConvertFloat4ToU32(kColorSelected));
    }

    // Expand/collapse toggle for inline echo (small triangle arrow).
    if (echo_panel_)
    {
        ImGui::PushID(info.name.c_str());
        const char* arrow = is_expanded ? ui::icon_str(ui::Icon::ChevronDown)
                                        : ui::icon_str(ui::Icon::ChevronRight);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
        if (ImGui::SmallButton(arrow))
        {
            if (is_expanded)
            {
                set_topic_expanded(info.name, false);
            }
            else
            {
                set_topic_expanded(info.name, true);
                cached_echo_msgs_.erase(info.name);
                // Select the topic to trigger echo subscription.
                selected_topic_ = info.name;
                if (select_cb_) select_cb_(info.name);
            }
        }
        ImGui::PopStyleColor(2);
        ImGui::PopID();
        ImGui::SameLine();
    }

    // Reuse Spectra's merged icon font so monitor status markers don't depend
    // on ad hoc Unicode glyph availability in the current atlas.
    const ImVec4 dot_col = stats.active ? kColorActive : kColorStale;
    ImGui::TextColored(dot_col, "%s", ui::icon_str(ui::Icon::Circle));
    ImGui::SameLine();

    // Leaf topic name (last segment).
    auto [ns, leaf] = split_namespace(info.name);
    const std::string display_name = group_by_namespace_ ? leaf : info.name;

    const ImGuiSelectableFlags sel_flags =
        ImGuiSelectableFlags_SpanAllColumns |
        ImGuiSelectableFlags_AllowOverlap;

    if (ImGui::Selectable(display_name.c_str(), is_selected, sel_flags,
                          ImVec2(0, 0))) {
        selected_topic_ = info.name;
        if (select_cb_) select_cb_(info.name);
    }

    // Drag source — whole topic with empty field_path.
    if (drag_drop_) {
        FieldDragPayload payload;
        payload.topic_name = info.name;
        payload.field_path = "";  // empty: caller picks first numeric field
        payload.type_name  = info.types.empty() ? "" : info.types[0];
        payload.label      = info.name;
        drag_drop_->begin_drag_source(payload);
        // Right-click context menu (unique id per row).
        std::string ctx_id = std::string("##tctx_") + info.name;
        drag_drop_->show_context_menu(payload, ctx_id.c_str());
    }

    // Double-click to plot.
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
        if (plot_cb_) plot_cb_(info.name);
    }

    // Tooltip on hover: full name + type.
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(info.name.c_str());
        if (!info.types.empty()) {
            ImGui::TextDisabled("%s", info.types[0].c_str());
        }
        ImGui::EndTooltip();
    }

    // Column 1: Type (first type, abbreviated + colored).
    int next_col = 1;
    if (col_show_type_) {
        ImGui::TableSetColumnIndex(next_col++);
        if (!info.types.empty()) {
            const std::string& t = info.types[0];
            const ImVec4 tc = topic_type_color(t);
            // Abbreviated leaf name after last '/'.
            const auto slash = t.rfind('/');
            const char* leaf_type = (slash != std::string::npos) ? t.c_str() + slash + 1 : t.c_str();
            ImGui::TextColored(tc, "%s", leaf_type);
        } else {
            ImGui::TextDisabled("-");
        }
    }

    // Column: Hz + sparkline.
    if (col_show_hz_) {
        ImGui::TableSetColumnIndex(next_col++);
        const float col_w   = ImGui::GetContentRegionAvail().x;
        const float spark_w = std::min(col_w * 0.5f, 36.0f);
        const float spark_h = ImGui::GetTextLineHeight() * 0.85f;
        const std::string hz_str = format_hz(stats.displayed_hz);
        if (stats.displayed_hz > 0.0)
            ImGui::TextUnformatted(hz_str.c_str());
        else
            ImGui::TextDisabled("%s", hz_str.c_str());
        if (!stats.hz_history.empty() && spark_w > 4.0f) {
            ImGui::SameLine(0.0f, 4.0f);
            draw_hz_sparkline(stats.hz_history, spark_w, spark_h);
        }
    }

    // Column: Pubs.
    if (col_show_pubs_) {
        ImGui::TableSetColumnIndex(next_col++);
        ImGui::Text("%d",
                    displayed_endpoint_count(info.publisher_count,
                                             info.local_publisher_count));
    }

    // Column: Subs.
    if (col_show_subs_) {
        ImGui::TableSetColumnIndex(next_col++);
        ImGui::Text("%d",
                    displayed_endpoint_count(info.subscriber_count,
                                             info.local_subscriber_count));
    }

    // Column: BW.
    if (col_show_bw_) {
        ImGui::TableSetColumnIndex(next_col++);
        const std::string bw_str = format_bw(stats.bandwidth_bps);
        if (stats.bandwidth_bps > 0.0)
            ImGui::TextUnformatted(bw_str.c_str());
        else
            ImGui::TextDisabled("%s", bw_str.c_str());
    }

    // --- Inline echo expansion (rqt-style) ---
    if (is_expanded && echo_panel_)
    {
        draw_inline_echo(info.name);
    }
}

bool TopicListPanel::is_topic_expanded(const std::string& topic_name) const
{
    return expanded_echo_topics_.find(topic_name) != expanded_echo_topics_.end();
}

void TopicListPanel::set_topic_expanded(const std::string& topic_name, bool expanded)
{
    if (expanded)
    {
        expanded_echo_topics_.insert(topic_name);
    }
    else
    {
        expanded_echo_topics_.erase(topic_name);
        cached_echo_msgs_.erase(topic_name);
    }
}

// ---------------------------------------------------------------------------
// draw_inline_echo — render latest echo message fields inside the topic table
// ---------------------------------------------------------------------------

void TopicListPanel::draw_inline_echo(const std::string& topic_name)
{
    // Refresh the cached message from the echo panel.
    if (echo_panel_->topic_name() == topic_name)
    {
        cached_echo_msgs_[topic_name] = echo_panel_->latest_message();
    }

    auto it = cached_echo_msgs_.find(topic_name);
    auto* cached_echo_msg =
        (it != cached_echo_msgs_.end()) ? it->second.get() : nullptr;

    // Insert a full-width row for the echo content.
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);

    if (!cached_echo_msg || cached_echo_msg->fields.empty())
    {
        ImGui::Indent(24.0f);
        ImGui::TextDisabled("(waiting for data...)");
        ImGui::Unindent(24.0f);
        return;
    }

    ImGui::Indent(24.0f);

    size_t idx = 0;
    while (idx < cached_echo_msg->fields.size())
    {
        draw_echo_field(topic_name,
                        cached_echo_msg->fields[idx],
                        idx,
                        cached_echo_msg->fields);
    }

    ImGui::Unindent(24.0f);
}

// ---------------------------------------------------------------------------
// draw_echo_field — render one field node in the inline echo tree
// ---------------------------------------------------------------------------

void TopicListPanel::draw_echo_field(const std::string& topic_name,
                                     EchoFieldValue& fv,
                                     size_t&         idx,
                                     const std::vector<EchoFieldValue>& all_fields)
{
    const float indent = static_cast<float>(fv.depth) * ImGui::GetStyle().IndentSpacing;
    if (indent > 0.0f) ImGui::Indent(indent);

    switch (fv.kind)
    {
        case EchoFieldValue::Kind::NestedHead:
        {
            ImGui::TextColored(kEchoNested, "%s:", fv.display_name.c_str());
            ++idx;
            const int parent_depth = fv.depth;
            while (idx < all_fields.size() && all_fields[idx].depth > parent_depth)
            {
                auto& child = const_cast<EchoFieldValue&>(all_fields[idx]);
                draw_echo_field(topic_name, child, idx, all_fields);
            }
            break;
        }
        case EchoFieldValue::Kind::ArrayHead:
        {
            char label[128];
            std::snprintf(label, sizeof(label), "%s  [%d items]",
                          fv.display_name.c_str(), fv.array_len);
            const std::string tree_id =
                std::string("##ie_") + topic_name + "_" + fv.path;
            const bool open = ImGui::TreeNodeEx(
                tree_id.c_str(), ImGuiTreeNodeFlags_SpanFullWidth, "%s", label);
            fv.is_open = open;
            ++idx;
            const int parent_depth = fv.depth;
            if (open)
            {
                while (idx < all_fields.size() && all_fields[idx].depth > parent_depth)
                {
                    auto& child = const_cast<EchoFieldValue&>(all_fields[idx]);
                    draw_echo_field(topic_name, child, idx, all_fields);
                }
                ImGui::TreePop();
            }
            else
            {
                while (idx < all_fields.size() && all_fields[idx].depth > parent_depth)
                    ++idx;
            }
            break;
        }
        case EchoFieldValue::Kind::ArrayElement:
        {
            char sel_id[256];
            std::snprintf(sel_id, sizeof(sel_id), "##topic_monitor_sel_%s", fv.path.c_str());
            ImGui::Selectable(sel_id,
                              false,
                              ImGuiSelectableFlags_AllowOverlap |
                                  ImGuiSelectableFlags_SpanAllColumns,
                              ImVec2(0, ImGui::GetTextLineHeight()));

            if (drag_drop_)
            {
                FieldDragPayload payload;
                payload.topic_name = topic_name;
                payload.field_path = fv.path;
                payload.type_name = selected_topic_ == topic_name && echo_panel_
                    ? echo_panel_->type_name()
                    : std::string();
                payload.label = FieldDragPayload::make_label(topic_name, fv.path);
                drag_drop_->begin_drag_source(payload);
                char ctx_id[280];
                std::snprintf(ctx_id, sizeof(ctx_id), "##topic_monitor_ctx_%s", fv.path.c_str());
                drag_drop_->show_context_menu(payload, ctx_id);
            }

            ImGui::SameLine();
            ImGui::TextColored(kEchoArray, "%-8s", fv.display_name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(kEchoNumeric, "%s",
                               TopicEchoPanel::format_numeric(fv.numeric).c_str());
            ++idx;
            break;
        }
        case EchoFieldValue::Kind::Numeric:
        {
            char sel_id[256];
            std::snprintf(sel_id, sizeof(sel_id), "##topic_monitor_sel_%s", fv.path.c_str());
            ImGui::Selectable(sel_id,
                              false,
                              ImGuiSelectableFlags_AllowOverlap |
                                  ImGuiSelectableFlags_SpanAllColumns,
                              ImVec2(0, ImGui::GetTextLineHeight()));

            if (drag_drop_)
            {
                FieldDragPayload payload;
                payload.topic_name = topic_name;
                payload.field_path = fv.path;
                payload.type_name = selected_topic_ == topic_name && echo_panel_
                    ? echo_panel_->type_name()
                    : std::string();
                payload.label = FieldDragPayload::make_label(topic_name, fv.path);
                drag_drop_->begin_drag_source(payload);
                char ctx_id[280];
                std::snprintf(ctx_id, sizeof(ctx_id), "##topic_monitor_ctx_%s", fv.path.c_str());
                drag_drop_->show_context_menu(payload, ctx_id);
            }

            ImGui::SameLine();
            ImGui::TextUnformatted(fv.display_name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(kEchoNumeric, "%s",
                               TopicEchoPanel::format_numeric(fv.numeric).c_str());
            ++idx;
            break;
        }
        case EchoFieldValue::Kind::Text:
        {
            ImGui::TextUnformatted(fv.display_name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(kEchoText, "\"%s\"", fv.text.c_str());
            ++idx;
            break;
        }
    }

    if (indent > 0.0f) ImGui::Unindent(indent);
}

#else   // !SPECTRA_USE_IMGUI

void TopicListPanel::draw(bool* /*p_open*/) {}
void TopicListPanel::draw_namespace_node(const std::string& /*ns*/, int /*depth*/) {}
void TopicListPanel::draw_topic_row(const TopicInfo& /*info*/, TopicStats& /*stats*/) {}
void TopicListPanel::draw_inline_echo(const std::string& /*topic_name*/) {}
void TopicListPanel::draw_echo_field(const std::string& /*topic_name*/,
                                     EchoFieldValue& /*fv*/, size_t& /*idx*/,
                                     const std::vector<EchoFieldValue>& /*all*/) {}

#endif  // SPECTRA_USE_IMGUI

}   // namespace spectra::adapters::ros2
