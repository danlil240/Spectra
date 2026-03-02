#include "ui/topic_stats_overlay.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <numeric>

#ifdef SPECTRA_USE_IMGUI
#include <imgui.h>
#endif

namespace spectra::adapters::ros2
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static int64_t wall_ns()
{
    using namespace std::chrono;
    return duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------------------
// TopicDetailStats
// ---------------------------------------------------------------------------

void TopicDetailStats::push(int64_t arrival_ns, size_t bytes, int64_t latency_us)
{
    // Drop detection: compute gap before inserting.
    if (last_msg_ns > 0) {
        last_gap_ns = arrival_ns - last_msg_ns;
    } else {
        last_gap_ns = 0;
    }

    samples.push_back({arrival_ns, bytes, latency_us});
    last_msg_ns = arrival_ns;
    ++total_messages;
    total_bytes += bytes;
}

void TopicDetailStats::compute(int64_t now_ns, int64_t window_ns)
{
    const int64_t cutoff = now_ns - window_ns;

    // Prune old samples.
    while (!samples.empty() && samples.front().arrival_ns < cutoff) {
        samples.pop_front();
    }

    const size_t n = samples.size();

    // --- Hz ---
    if (n < 2) {
        hz_avg = (n == 1) ? 1.0 : 0.0;
        hz_min = hz_avg;
        hz_max = hz_avg;
    } else {
        // Span-based average Hz.
        const double span_s =
            static_cast<double>(samples.back().arrival_ns - samples.front().arrival_ns) *
            1e-9;
        hz_avg = (span_s > 0.0) ? static_cast<double>(n - 1) / span_s : 0.0;

        // Instantaneous Hz between consecutive pairs.
        double inst_min = 1e18;
        double inst_max = 0.0;
        for (size_t i = 1; i < n; ++i) {
            const double gap_s =
                static_cast<double>(samples[i].arrival_ns - samples[i - 1].arrival_ns) *
                1e-9;
            if (gap_s > 0.0) {
                const double inst = 1.0 / gap_s;
                if (inst < inst_min) inst_min = inst;
                if (inst > inst_max) inst_max = inst;
            }
        }
        hz_min = (inst_min < 1e17) ? inst_min : 0.0;
        hz_max = inst_max;
    }

    // --- Bandwidth ---
    if (n < 1) {
        bw_bps = 0.0;
    } else {
        uint64_t total = 0;
        for (const auto& s : samples)
            total += s.bytes;
        const double win_s = static_cast<double>(window_ns) * 1e-9;
        bw_bps = static_cast<double>(total) / win_s;
    }

    // --- Latency (only samples with valid latency) ---
    {
        double sum = 0.0;
        double lmin = 1e18;
        double lmax = -1e18;
        int    count = 0;
        for (const auto& s : samples) {
            if (s.latency_us >= 0) {
                const double v = static_cast<double>(s.latency_us);
                sum += v;
                if (v < lmin) lmin = v;
                if (v > lmax) lmax = v;
                ++count;
            }
        }
        if (count > 0) {
            latency_avg_us = sum / count;
            latency_min_us = lmin;
            latency_max_us = lmax;
        } else {
            latency_avg_us = -1.0;
            latency_min_us = -1.0;
            latency_max_us = -1.0;
        }
    }

    // --- Drop detection ---
    if (hz_avg > 0.0 && last_gap_ns > 0) {
        const double expected_gap_ns = 1e9 / hz_avg;
        // Use the stored drop_detected flag; caller sets drop_factor externally.
        // Here we compare: if last_gap_ns > 3× expected, flag it.
        // The actual factor is applied in TopicStatsOverlay::compute_now().
        drop_detected = false;  // reset; TopicStatsOverlay sets this.
    } else {
        drop_detected = false;
    }
}

void TopicDetailStats::reset_window()
{
    samples.clear();
    hz_avg         = 0.0;
    hz_min         = 0.0;
    hz_max         = 0.0;
    bw_bps         = 0.0;
    latency_avg_us = -1.0;
    latency_min_us = -1.0;
    latency_max_us = -1.0;
    drop_detected  = false;
    last_gap_ns    = 0;
    last_msg_ns    = 0;
}

// ---------------------------------------------------------------------------
// TopicStatsOverlay
// ---------------------------------------------------------------------------

TopicStatsOverlay::TopicStatsOverlay() = default;

void TopicStatsOverlay::set_topic(const std::string& topic_name)
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (topic_name == current_topic_)
        return;
    current_topic_ = topic_name;
    stats_.reset_window();
    // Keep cumulative counters from previous topic? No — new topic, fresh slate.
    stats_.total_messages = 0;
    stats_.total_bytes    = 0;
}

const std::string& TopicStatsOverlay::topic() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return current_topic_;
}

void TopicStatsOverlay::notify_message(const std::string& topic_name,
                                       size_t bytes,
                                       int64_t latency_us)
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (topic_name != current_topic_ || current_topic_.empty())
        return;
    stats_.push(wall_ns(), bytes, latency_us);
}

void TopicStatsOverlay::reset_stats()
{
    std::lock_guard<std::mutex> lk(mutex_);
    stats_.reset_window();
}

void TopicStatsOverlay::set_window_ms(int ms)
{
    window_ms_ = (ms > 0) ? ms : 100;
}

void TopicStatsOverlay::set_drop_factor(double factor)
{
    drop_factor_ = (factor > 1.0) ? factor : 2.0;
}

void TopicStatsOverlay::compute_now(int64_t now_ns_val)
{
    std::lock_guard<std::mutex> lk(mutex_);
    const int64_t window_ns = static_cast<int64_t>(window_ms_) * 1'000'000LL;
    stats_.compute(now_ns_val, window_ns);

    // Apply drop detection using our factor.
    if (stats_.hz_avg > 0.0 && stats_.last_gap_ns > 0) {
        const double expected_ns = 1e9 / stats_.hz_avg;
        stats_.drop_detected =
            static_cast<double>(stats_.last_gap_ns) > drop_factor_ * expected_ns;
    } else {
        stats_.drop_detected = false;
    }
}

TopicStatsOverlay::StatsSnapshot TopicStatsOverlay::snapshot()
{
    const int64_t window_ns = static_cast<int64_t>(window_ms_) * 1'000'000LL;
    std::lock_guard<std::mutex> lk(mutex_);
    stats_.compute(wall_ns(), window_ns);
    if (stats_.hz_avg > 0.0 && stats_.last_gap_ns > 0) {
        const double expected_ns = 1e9 / stats_.hz_avg;
        stats_.drop_detected =
            static_cast<double>(stats_.last_gap_ns) > drop_factor_ * expected_ns;
    } else {
        stats_.drop_detected = false;
    }

    StatsSnapshot snap;
    snap.topic           = current_topic_;
    snap.hz_avg          = stats_.hz_avg;
    snap.hz_min          = stats_.hz_min;
    snap.hz_max          = stats_.hz_max;
    snap.bw_bps          = stats_.bw_bps;
    snap.latency_avg_us  = stats_.latency_avg_us;
    snap.latency_min_us  = stats_.latency_min_us;
    snap.latency_max_us  = stats_.latency_max_us;
    snap.total_messages  = stats_.total_messages;
    snap.total_bytes     = stats_.total_bytes;
    snap.drop_detected   = stats_.drop_detected;
    snap.last_gap_ns     = stats_.last_gap_ns;
    return snap;
}

// ---------------------------------------------------------------------------
// Formatting helpers
// ---------------------------------------------------------------------------

std::string TopicStatsOverlay::format_hz(double hz)
{
    if (hz <= 0.0)
        return "—";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", hz);
    return buf;
}

std::string TopicStatsOverlay::format_bw(double bps)
{
    if (bps <= 0.0)
        return "—";
    char buf[32];
    if (bps >= 1024.0 * 1024.0) {
        std::snprintf(buf, sizeof(buf), "%.2f MB/s", bps / (1024.0 * 1024.0));
    } else if (bps >= 1024.0) {
        std::snprintf(buf, sizeof(buf), "%.1f KB/s", bps / 1024.0);
    } else {
        std::snprintf(buf, sizeof(buf), "%.0f B/s", bps);
    }
    return buf;
}

std::string TopicStatsOverlay::format_bytes(uint64_t bytes)
{
    char buf[32];
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        std::snprintf(buf, sizeof(buf), "%.2f GB",
                      static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024ULL * 1024ULL) {
        std::snprintf(buf, sizeof(buf), "%.2f MB",
                      static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else if (bytes >= 1024ULL) {
        std::snprintf(buf, sizeof(buf), "%.1f KB",
                      static_cast<double>(bytes) / 1024.0);
    } else {
        std::snprintf(buf, sizeof(buf), "%llu B",
                      static_cast<unsigned long long>(bytes));
    }
    return buf;
}

std::string TopicStatsOverlay::format_latency(double us)
{
    if (us < 0.0)
        return "—";
    char buf[32];
    if (us >= 1000.0) {
        std::snprintf(buf, sizeof(buf), "%.2f ms", us / 1000.0);
    } else {
        std::snprintf(buf, sizeof(buf), "%.1f µs", us);
    }
    return buf;
}

int64_t TopicStatsOverlay::now_ns()
{
    return wall_ns();
}

// ---------------------------------------------------------------------------
// ImGui rendering
// ---------------------------------------------------------------------------

#ifdef SPECTRA_USE_IMGUI

void TopicStatsOverlay::draw_stat_row(const char* label, const std::string& value,
                                       bool highlight)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextDisabled("%s", label);
    ImGui::TableSetColumnIndex(1);
    if (highlight) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
        ImGui::TextUnformatted(value.c_str());
        ImGui::PopStyleColor();
    } else {
        ImGui::TextUnformatted(value.c_str());
    }
}

void TopicStatsOverlay::draw_inline()
{
    // Compute stats under lock, then render without lock.
    StatsSnapshot snap = snapshot();

    if (snap.topic.empty()) {
        ImGui::TextDisabled("No topic selected.");
        return;
    }

    // Topic name header.
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.85f, 1.0f, 1.0f));
    ImGui::TextUnformatted(snap.topic.c_str());
    ImGui::PopStyleColor();
    ImGui::Separator();

    // Drop warning banner.
    if (snap.drop_detected) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        ImGui::TextUnformatted("  Drop detected!");
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            char tip[128];
            std::snprintf(tip, sizeof(tip),
                          "Last inter-message gap: %.1f ms  (> %.0f× expected)",
                          static_cast<double>(snap.last_gap_ns) * 1e-6,
                          drop_factor_);
            ImGui::SetTooltip("%s", tip);
        }
        ImGui::Separator();
    }

    constexpr ImGuiTableFlags kTableFlags =
        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoHostExtendX;

    if (ImGui::BeginTable("##stats_table", 2, kTableFlags)) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

        // --- Hz section ---
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        ImGui::TextUnformatted("FREQUENCY");
        ImGui::PopStyleColor();

        draw_stat_row("  Avg Hz",     format_hz(snap.hz_avg));
        draw_stat_row("  Min Hz",     format_hz(snap.hz_min));
        draw_stat_row("  Max Hz",     format_hz(snap.hz_max));

        // --- Message count ---
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        ImGui::TextUnformatted("MESSAGES");
        ImGui::PopStyleColor();

        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%llu",
                          static_cast<unsigned long long>(snap.total_messages));
            draw_stat_row("  Total count", buf);
        }

        // --- Bandwidth ---
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        ImGui::TextUnformatted("BANDWIDTH");
        ImGui::PopStyleColor();

        draw_stat_row("  Rate",       format_bw(snap.bw_bps));
        draw_stat_row("  Total",      format_bytes(snap.total_bytes));

        // --- Latency ---
        if (snap.latency_avg_us >= 0.0) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
            ImGui::TextUnformatted("LATENCY");
            ImGui::PopStyleColor();

            draw_stat_row("  Avg",     format_latency(snap.latency_avg_us));
            draw_stat_row("  Min",     format_latency(snap.latency_min_us));
            draw_stat_row("  Max",     format_latency(snap.latency_max_us));
        } else {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
            ImGui::TextUnformatted("LATENCY");
            ImGui::PopStyleColor();
            draw_stat_row("  Avg", "— (no header)");
        }

        // --- Drop info ---
        {
            char buf[64];
            if (snap.last_gap_ns > 0) {
                std::snprintf(buf, sizeof(buf), "%.1f ms",
                              static_cast<double>(snap.last_gap_ns) * 1e-6);
            } else {
                std::snprintf(buf, sizeof(buf), "—");
            }
            draw_stat_row("  Last gap", buf, snap.drop_detected);
        }

        ImGui::EndTable();
    }
}

void TopicStatsOverlay::draw(bool* p_open)
{
    if (!ImGui::Begin(title_.c_str(), p_open)) {
        ImGui::End();
        return;
    }
    draw_inline();
    ImGui::End();
}

#else  // SPECTRA_USE_IMGUI

void TopicStatsOverlay::draw_stat_row(const char* /*label*/,
                                       const std::string& /*value*/,
                                       bool /*highlight*/)
{}

void TopicStatsOverlay::draw_inline()
{}

void TopicStatsOverlay::draw(bool* /*p_open*/)
{}

#endif  // SPECTRA_USE_IMGUI

}   // namespace spectra::adapters::ros2
