#include "ui/topic_list_panel.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#ifdef SPECTRA_USE_IMGUI
#include <imgui.h>
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

    const int64_t stale_ns = 2'000'000'000LL;  // 2 s
    active = (last_msg_ns > 0) && (now_ns - last_msg_ns < stale_ns);
}

// ---------------------------------------------------------------------------
// TopicListPanel — construction
// ---------------------------------------------------------------------------

TopicListPanel::TopicListPanel()
{
    std::memset(filter_buf_, 0, sizeof(filter_buf_));
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
    if (hz <= 0.0) return "—";
    char buf[32];
    if (hz >= 100.0)
        std::snprintf(buf, sizeof(buf), "%.0f", hz);
    else if (hz >= 10.0)
        std::snprintf(buf, sizeof(buf), "%.1f", hz);
    else
        std::snprintf(buf, sizeof(buf), "%.2f", hz);
    return buf;
}

/*static*/ std::string TopicListPanel::format_bw(double bps)
{
    if (bps <= 0.0) return "—";
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
static constexpr ImVec4 kColorActive  = {0.20f, 0.80f, 0.30f, 1.0f};  // green
static constexpr ImVec4 kColorStale   = {0.50f, 0.50f, 0.50f, 1.0f};  // gray
static constexpr ImVec4 kColorSelected = {0.26f, 0.59f, 0.98f, 0.25f}; // blue tint

void TopicListPanel::draw(bool* p_open)
{
    // Poll discovery for topic list updates.
    if (disc_) {
        auto fresh = disc_->topics();
        std::lock_guard<std::mutex> lk(topics_mutex_);
        if (fresh.size() != topics_.size()) {
            topics_ = std::move(fresh);
            rebuild_tree();
            filter_dirty_ = true;
        } else {
            // Check if any name changed.
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

    // Prune and compute stats under stats_mutex_ once per frame.
    const int64_t cur_ns = wall_clock_ns();
    const int64_t win_ns = static_cast<int64_t>(stats_window_ms_) * 1'000'000LL;
    {
        std::lock_guard<std::mutex> lk(stats_mutex_);
        for (auto& [name, st] : stats_map_) {
            st.prune_and_compute(cur_ns, win_ns);
        }
    }

    // --- Window ---
    ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (!ImGui::Begin(title_.c_str(), p_open, flags)) {
        ImGui::End();
        return;
    }

    // --- Search bar ---
    ImGui::SetNextItemWidth(-80.0f);
    const bool filter_changed =
        ImGui::InputTextWithHint("##filter", "Search topics...", filter_buf_, sizeof(filter_buf_));
    if (filter_changed) {
        filter_str_   = filter_buf_;
        filter_dirty_ = true;
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

    if (!ImGui::BeginTable("##topics", 6, kTableFlags, table_sz)) {
        ImGui::End();
        return;
    }

    ImGui::TableSetupScrollFreeze(0, 1);  // freeze header row
    ImGui::TableSetupColumn("Topic",    ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("Type",     ImGuiTableColumnFlags_WidthStretch, 2.5f);
    ImGui::TableSetupColumn("Hz",       ImGuiTableColumnFlags_WidthFixed,   52.0f);
    ImGui::TableSetupColumn("Pubs",     ImGuiTableColumnFlags_WidthFixed,   38.0f);
    ImGui::TableSetupColumn("Subs",     ImGuiTableColumnFlags_WidthFixed,   38.0f);
    ImGui::TableSetupColumn("BW",       ImGuiTableColumnFlags_WidthFixed,   72.0f);
    ImGui::TableHeadersRow();

    // --- Rows ---
    std::lock_guard<std::mutex> lk_t(topics_mutex_);

    if (group_by_namespace_) {
        // Root-level loose topics first (e.g. /rosout at "/").
        for (const auto& tname : root_topics_) {
            if (!filter_str_.empty() && tname.find(filter_str_) == std::string::npos)
                continue;
            // Find TopicInfo.
            auto it = std::find_if(topics_.begin(), topics_.end(),
                                   [&](const TopicInfo& ti) { return ti.name == tname; });
            if (it == topics_.end()) continue;
            TopicStats* stats_ptr = nullptr;
            {
                std::lock_guard<std::mutex> lk_s(stats_mutex_);
                stats_ptr = &stats_map_[tname];  // ensure entry exists
            }
            // We temporarily unlock stats to avoid nested lock; we only read here.
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
        // Leaf topics.
        std::lock_guard<std::mutex> lk_s(stats_mutex_);
        for (const auto& tname : node.topic_names) {
            if (!filter_str_.empty() && tname.find(filter_str_) == std::string::npos)
                continue;
            auto tit = std::find_if(topics_.begin(), topics_.end(),
                                    [&](const TopicInfo& ti) { return ti.name == tname; });
            if (tit == topics_.end()) continue;
            draw_topic_row(*tit, stats_map_[tname]);
        }

        // Child namespaces.
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
    if (is_selected) {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                               ImGui::ColorConvertFloat4ToU32(kColorSelected));
    }

    // Status dot (colored circle) via text.
    const ImVec4 dot_col = stats.active ? kColorActive : kColorStale;
    ImGui::TextColored(dot_col, "%s", stats.active ? "●" : "○");
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

    // Column 1: Type (first type, abbreviated).
    ImGui::TableSetColumnIndex(1);
    if (!info.types.empty()) {
        // Abbreviate: drop "xxx_msgs/msg/" prefix.
        const std::string& t = info.types[0];
        const auto slash = t.rfind('/');
        ImGui::TextUnformatted(slash != std::string::npos ? t.c_str() + slash + 1 : t.c_str());
    } else {
        ImGui::TextDisabled("—");
    }

    // Column 2: Hz.
    ImGui::TableSetColumnIndex(2);
    const std::string hz_str = format_hz(stats.hz);
    if (stats.hz > 0.0)
        ImGui::TextUnformatted(hz_str.c_str());
    else
        ImGui::TextDisabled("%s", hz_str.c_str());

    // Column 3: Pubs.
    ImGui::TableSetColumnIndex(3);
    ImGui::Text("%d", info.publisher_count);

    // Column 4: Subs.
    ImGui::TableSetColumnIndex(4);
    ImGui::Text("%d", info.subscriber_count);

    // Column 5: BW.
    ImGui::TableSetColumnIndex(5);
    const std::string bw_str = format_bw(stats.bandwidth_bps);
    if (stats.bandwidth_bps > 0.0)
        ImGui::TextUnformatted(bw_str.c_str());
    else
        ImGui::TextDisabled("%s", bw_str.c_str());
}

#else   // !SPECTRA_USE_IMGUI

void TopicListPanel::draw(bool* /*p_open*/) {}
void TopicListPanel::draw_namespace_node(const std::string& /*ns*/, int /*depth*/) {}
void TopicListPanel::draw_topic_row(const TopicInfo& /*info*/, TopicStats& /*stats*/) {}

#endif  // SPECTRA_USE_IMGUI

}   // namespace spectra::adapters::ros2
