// builtin_providers.cpp — Data sidebar adapters for logs, topics and CSV files.

#include "ui/data_sidebar/builtin_providers.hpp"

#include <spectra/axes.hpp>
#include <spectra/series.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <span>

#include "ui/data_sidebar/data_sidebar_model.hpp"
#include "ui/figures/figure_manager.hpp"
#include "ui/logs/log_browser_panel.hpp"
#include "ui/topics/topics_panel.hpp"
#include "ui/workspace/data_binding.hpp"

namespace spectra::ui::data_sidebar
{

namespace
{

std::string count_text(size_t n, const char* one, const char* many)
{
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

template <typename T>
DragPayload make_payload(const char* type, const T& value)
{
    DragPayload out;
    out.type = type;
    out.data.resize(sizeof(T));
    std::memcpy(out.data.data(), &value, sizeof(T));
    return out;
}

}   // namespace

// ─── LogFileProvider ─────────────────────────────────────────────────────────

void LogFileProvider::list_sources(std::vector<DataSource>& out)
{
    if (!panel_)
        return;
    for (const auto& file : panel_->files())
    {
        if (!file.reader)
            continue;
        const auto& fields = file.reader->fields();
        DataSource  src;
        src.name   = file.name;
        src.key    = file.id;
        src.detail = std::string(file.reader->format_name());
        src.fields.reserve(fields.size());
        for (size_t i = 0; i < fields.size(); ++i)
        {
            const auto& lf = fields[i];
            DataField   f;
            if (lf.path.empty())
            {
                f.name = lf.topic;
            }
            else
            {
                f.group = lf.topic;
                f.name  = lf.path;
            }
            f.detail = std::to_string(lf.sample_count);
            if (!lf.unit.empty())
                f.detail += " " + lf.unit;
            f.key = i;
            src.fields.push_back(std::move(f));
        }
        out.push_back(std::move(src));
    }
}

PlotResult LogFileProvider::plot_field(uint64_t source_key, uint64_t field_key, const PlotTarget& t)
{
    if (!panel_)
        return {false, "Log files are not available"};
    LineSeries* s = panel_->plot_field(static_cast<uint32_t>(source_key),
                                       static_cast<size_t>(field_key),
                                       t.axes);
    return {s != nullptr, panel_->status()};
}

DragPayload LogFileProvider::drag_payload(uint64_t source_key, uint64_t field_key) const
{
    logs::LogFieldDragPayload p;
    p.file_id     = static_cast<uint32_t>(source_key);
    p.field_index = static_cast<uint32_t>(field_key);
    return make_payload(logs::LOG_FIELD_DRAG_TYPE, p);
}

bool LogFileProvider::close_source(uint64_t source_key)
{
    if (!panel_ || !panel_->find_file(static_cast<uint32_t>(source_key)))
        return false;
    panel_->close_file(static_cast<uint32_t>(source_key));
    return true;
}

void LogFileProvider::open()
{
    if (open_dialog_)
        open_dialog_();
}

void LogFileProvider::open_details()
{
    if (panel_)
        panel_->set_visible(true);
}

std::string LogFileProvider::empty_hint() const
{
    return "Open a ULog or MCAP file, or drop one onto the window.";
}

// ─── TopicProvider ───────────────────────────────────────────────────────────

uint64_t TopicProvider::key_for(const std::string& name)
{
    // Never PUBLISHED_SOURCE_KEY.
    const uint64_t h = static_cast<uint64_t>(std::hash<std::string>{}(name));
    return h == PUBLISHED_SOURCE_KEY ? 1 : h;
}

void TopicProvider::list_sources(std::vector<DataSource>& out)
{
    if (!panel_)
        return;
    // Keep the list current even while the Topics panel itself is closed.
    panel_->request_list();

    std::vector<ipc::TopicInfoEntry> topics = panel_->topics();
    std::sort(topics.begin(),
              topics.end(),
              [](const ipc::TopicInfoEntry& a, const ipc::TopicInfoEntry& b)
              { return a.name < b.name; });
    for (const auto& t : topics)
        topics_by_key_[key_for(t.name)] = t;

    auto make_field = [](const ipc::TopicInfoEntry& t, const std::string& shown)
    {
        DataField f;
        f.name = shown;
        char rate[32];
        std::snprintf(rate, sizeof(rate), "%.0f Hz", t.estimated_hz);
        f.detail = std::string(t.kind == ipc::TopicKind::Scalar3D ? "3D " : "") + rate;
        if (!t.publisher_online)
            f.detail += " (offline)";
        f.key = key_for(t.name);
        return f;
    };

    // Plugin data sources: their points are published as "<source>/<label>".
    std::vector<char> owned(topics.size(), 0);
    for (const auto& row : panel_->data_sources())
    {
        DataSource src;
        src.name                 = row.name;
        src.key                  = key_for("source:" + row.name);
        src.state                = row.faulted
                                       ? DataSource::State::Faulted
                                       : (row.running ? DataSource::State::Running : DataSource::State::Stopped);
        src.detail               = row.faulted ? "faulted" : (row.running ? "running" : "stopped");
        source_names_[src.key]   = row.name;
        const std::string prefix = row.name + "/";
        for (size_t i = 0; i < topics.size(); ++i)
        {
            if (topics[i].name.rfind(prefix, 0) == 0)
            {
                owned[i] = 1;
                src.fields.push_back(make_field(topics[i], topics[i].name.substr(prefix.size())));
            }
        }
        out.push_back(std::move(src));
    }

    // Everything else, grouped by parent path ("imu/accel_x" -> imu / accel_x).
    DataSource pub;
    pub.name  = "Topics";
    pub.key   = PUBLISHED_SOURCE_KEY;
    pub.state = DataSource::State::Static;
    for (size_t i = 0; i < topics.size(); ++i)
    {
        if (owned[i])
            continue;
        const std::string& name  = topics[i].name;
        const size_t       slash = name.find_last_of('/');
        std::string        group;
        std::string        leaf = name;
        if (slash != std::string::npos && slash > 0 && slash + 1 < name.size())
        {
            group = name.substr(0, slash);
            leaf  = name.substr(slash + 1);
        }
        DataField f = make_field(topics[i], leaf);
        f.group     = std::move(group);
        pub.fields.push_back(std::move(f));
    }
    // Sorting by name keeps groups contiguous except where a parent path is a
    // prefix of a sibling; stable-sort by group to be sure.
    std::stable_sort(pub.fields.begin(),
                     pub.fields.end(),
                     [](const DataField& a, const DataField& b) { return a.group < b.group; });
    pub.detail = count_text(pub.fields.size(), "topic", "topics");
    if (!pub.fields.empty())
        out.push_back(std::move(pub));
}

PlotResult TopicProvider::plot_field(uint64_t /*source_key*/,
                                     uint64_t          field_key,
                                     const PlotTarget& t)
{
    if (!panel_)
        return {false, "Topics are not available"};
    const auto it = topics_by_key_.find(field_key);
    if (it == topics_by_key_.end())
        return {false, "That topic is no longer published"};
    const std::string& name = it->second.name;
    if (!panel_->submit_subscribe(name, t.figure_id, t.axes_index))
        return {false, "Topics are not connected in this window"};
    return {true, "Subscribed to " + name};
}

DragPayload TopicProvider::drag_payload(uint64_t /*source_key*/, uint64_t field_key) const
{
    const auto it = topics_by_key_.find(field_key);
    if (it == topics_by_key_.end())
        return {};
    return make_payload(topics::TOPIC_DRAG_TYPE, topics::TopicsPanel::make_payload(it->second));
}

bool TopicProvider::set_source_running(uint64_t source_key, bool run)
{
    const auto it = source_names_.find(source_key);
    if (!panel_ || it == source_names_.end())
        return false;
    return panel_->submit_data_source_toggle(it->second, run);
}

void TopicProvider::open_details()
{
    if (panel_)
        panel_->set_visible(true);
}

std::string TopicProvider::empty_hint() const
{
    if (panel_ && !panel_->has_subscribe_callback())
        return "Topics are not connected in this window.";
    return "No topics published yet. Start a UDP bridge, a plugin source or a Python publisher.";
}

// ─── CsvProvider ─────────────────────────────────────────────────────────────

int CsvProvider::x_column(const CsvData& data)
{
    return data.num_cols >= 2 ? 0 : -1;
}

uint64_t CsvProvider::add_file(const std::string& path, CsvData data)
{
    if (!data.error.empty() || data.num_cols == 0)
        return 0;
    files_.erase(
        std::remove_if(files_.begin(), files_.end(), [&](const File& f) { return f.path == path; }),
        files_.end());
    File f;
    f.id   = next_id_++;
    f.path = path;
    f.name = std::filesystem::path(path).filename().string();
    f.data = std::move(data);
    files_.push_back(std::move(f));
    return files_.back().id;
}

uint64_t CsvProvider::open_file(const std::string& path)
{
    return add_file(path, parse_csv(path));
}

void CsvProvider::list_sources(std::vector<DataSource>& out)
{
    for (const auto& file : files_)
    {
        const CsvData& d = file.data;
        DataSource     src;
        src.name     = file.name;
        src.key      = file.id;
        src.detail   = count_text(d.num_rows, "row", "rows");
        const int xc = x_column(d);
        for (size_t c = 0; c < d.num_cols; ++c)
        {
            DataField f;
            f.name   = c < d.headers.size() && !d.headers[c].empty()
                           ? d.headers[c]
                           : "column " + std::to_string(c + 1);
            f.detail = static_cast<int>(c) == xc ? "x" : "";
            f.key    = c;
            src.fields.push_back(std::move(f));
        }
        out.push_back(std::move(src));
    }
}

PlotResult CsvProvider::plot_field(uint64_t source_key, uint64_t field_key, const PlotTarget& t)
{
    const auto it = std::find_if(files_.begin(),
                                 files_.end(),
                                 [&](const File& f) { return f.id == source_key; });
    if (it == files_.end())
        return {false, "That CSV file is no longer loaded"};
    const CsvData& d = it->data;
    if (field_key >= d.num_cols || field_key >= d.columns.size())
        return {false, "Unknown column"};
    if (d.num_rows == 0)
        return {false, it->name + " has no rows"};
    if (!t.axes)
        return {false, "No plot to add the column to"};

    const size_t yc     = static_cast<size_t>(field_key);
    auto         header = [&](size_t c)
    {
        return c < d.headers.size() && !d.headers[c].empty() ? d.headers[c]
                                                             : "column " + std::to_string(c + 1);
    };
    const std::vector<float>& y = d.columns[yc];

    // The x column against itself is not useful: plot it against row number.
    const int          xc = x_column(d);
    std::vector<float> index;
    const bool         use_index = xc < 0 || static_cast<size_t>(xc) == yc;
    if (use_index)
    {
        index.resize(y.size());
        for (size_t i = 0; i < index.size(); ++i)
            index[i] = static_cast<float>(i);
    }
    const std::vector<float>& x = use_index ? index : d.columns[static_cast<size_t>(xc)];
    const size_t              n = std::min(x.size(), y.size());

    Axes&       ax           = *t.axes;
    const bool  first_series = ax.series().empty();
    LineSeries& line =
        ax.line(std::span<const float>(x.data(), n), std::span<const float>(y.data(), n));
    line.label(header(yc));
    if (!use_index)
    {
        const size_t x_idx = static_cast<size_t>(xc);
        line.set_data_binding(csv_binding_uri(it->name, header(x_idx), header(yc)));
        if (x_idx < d.column_offsets.size())
            line.x_offset(d.column_offsets[x_idx]);
        if (first_series && ax.xlabel().empty())
            ax.xlabel(header(x_idx));
    }
    if (yc < d.column_offsets.size() && d.column_offsets[yc] != 0.0)
        line.y_offset(d.column_offsets[yc]);
    ax.auto_fit();
    return {true, "Plotted " + header(yc) + " (" + count_text(n, "row", "rows") + ")"};
}

bool CsvProvider::close_source(uint64_t source_key)
{
    const auto before = files_.size();
    files_.erase(std::remove_if(files_.begin(),
                                files_.end(),
                                [&](const File& f) { return f.id == source_key; }),
                 files_.end());
    return files_.size() != before;
}

void CsvProvider::open()
{
    if (open_dialog_)
        open_dialog_();
}

std::string CsvProvider::empty_hint() const
{
    return "Load a CSV file, or drop one onto the window.";
}

// ─── Installation ────────────────────────────────────────────────────────────

CsvProvider* install_builtin_providers(DataSidebarModel& model, const BuiltinProviderHooks& hooks)
{
    if (hooks.log_panel)
        model.add_provider(
            std::make_unique<LogFileProvider>(hooks.log_panel, hooks.open_log_dialog));
    auto* csv = static_cast<CsvProvider*>(
        model.add_provider(std::make_unique<CsvProvider>(hooks.open_csv_dialog)));
    if (hooks.topics_panel)
        model.add_provider(std::make_unique<TopicProvider>(hooks.topics_panel));
    return csv;
}

PlotTarget active_figure_target(FigureManager& figures)
{
    PlotTarget t;
    FigureId   id  = figures.active_index();
    Figure*    fig = figures.registry().get(id);
    if (!fig)
    {
        id  = figures.create_figure(FigureConfig{});
        fig = figures.registry().get(id);
        if (!fig)
            return t;
    }
    Axes* ax        = (!fig->axes().empty() && fig->axes().front()) ? fig->axes().front().get()
                                                                    : &fig->subplot(1, 1, 1);
    t.axes          = ax;
    t.figure_id     = id;
    const auto& all = fig->all_axes();
    for (size_t i = 0; i < all.size(); ++i)
    {
        if (all[i].get() == ax)
            t.axes_index = static_cast<uint32_t>(i);
    }
    return t;
}

}   // namespace spectra::ui::data_sidebar
