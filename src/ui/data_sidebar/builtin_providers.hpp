// builtin_providers.hpp — Data sidebar adapters for the built-in sources.
//
//   LogFileProvider  — files open in the Log Browser (ULog, MCAP, ...)
//   TopicProvider    — live topics (UDP / plugin bridges, Python publishers)
//                      and plugin data sources from the Topics panel
//   CsvProvider      — CSV / TSV files loaded through File > Load CSV or drop
//
// Each is a thin wrapper: the panels keep owning the data and stay usable as
// the sidebar's "details" windows.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ipc/message.hpp"
#include "ui/data/csv_loader.hpp"
#include "ui/data_sidebar/data_provider.hpp"

namespace spectra::ui::logs
{
class LogBrowserPanel;
}
namespace spectra::ui::topics
{
class TopicsPanel;
}

namespace spectra::ui::data_sidebar
{

class DataSidebarModel;

// ─── Log files ──────────────────────────────────────────────────────────────

class LogFileProvider final : public DataProvider
{
   public:
    // `open_dialog` shows the host's "Open Log" file dialog (may be empty).
    explicit LogFileProvider(logs::LogBrowserPanel* panel, std::function<void()> open_dialog = {})
        : panel_(panel), open_dialog_(std::move(open_dialog))
    {
    }

    std::string_view id() const override { return "logs"; }
    std::string_view title() const override { return "Log files"; }
    Icon             icon() const override { return Icon::FolderOpen; }

    void        list_sources(std::vector<DataSource>& out) override;
    PlotResult  plot_field(uint64_t source_key, uint64_t field_key, const PlotTarget& t) override;
    DragPayload drag_payload(uint64_t source_key, uint64_t field_key) const override;
    bool        supports_close() const override { return true; }
    bool        close_source(uint64_t source_key) override;

    std::string open_label() const override { return open_dialog_ ? "Open Log..." : ""; }
    void        open() override;
    std::string details_label() const override { return "Show Log Browser"; }
    void        open_details() override;
    std::string empty_hint() const override;

   private:
    logs::LogBrowserPanel* panel_ = nullptr;
    std::function<void()>  open_dialog_;
};

// ─── Live topics ────────────────────────────────────────────────────────────

class TopicProvider final : public DataProvider
{
   public:
    explicit TopicProvider(topics::TopicsPanel* panel) : panel_(panel) {}

    std::string_view id() const override { return "topics"; }
    std::string_view title() const override { return "Live topics"; }
    Icon             icon() const override { return Icon::Broadcast; }

    void        list_sources(std::vector<DataSource>& out) override;
    PlotResult  plot_field(uint64_t source_key, uint64_t field_key, const PlotTarget& t) override;
    DragPayload drag_payload(uint64_t source_key, uint64_t field_key) const override;
    bool        set_source_running(uint64_t source_key, bool run) override;

    std::string details_label() const override { return "Show Topics Panel"; }
    void        open_details() override;
    std::string empty_hint() const override;

    // Source key of the "Topics" source that holds topics no plugin source owns.
    static constexpr uint64_t PUBLISHED_SOURCE_KEY = 0;

    // Key used for a topic / plugin-source name (stable across refreshes).
    static uint64_t key_for(const std::string& name);

   private:
    topics::TopicsPanel*                              panel_ = nullptr;
    std::unordered_map<uint64_t, ipc::TopicInfoEntry> topics_by_key_;
    std::unordered_map<uint64_t, std::string>         source_names_;
};

// ─── CSV files ──────────────────────────────────────────────────────────────

class CsvProvider final : public DataProvider
{
   public:
    struct File
    {
        uint64_t    id = 0;
        std::string path;
        std::string name;   // file name without directories
        CsvData     data;
    };

    // `open_dialog` shows the host's CSV file dialog (may be empty).
    explicit CsvProvider(std::function<void()> open_dialog = {})
        : open_dialog_(std::move(open_dialog))
    {
    }

    std::string_view id() const override { return "csv"; }
    std::string_view title() const override { return "CSV files"; }
    Icon             icon() const override { return Icon::FileText; }

    void       list_sources(std::vector<DataSource>& out) override;
    PlotResult plot_field(uint64_t source_key, uint64_t field_key, const PlotTarget& t) override;
    bool       supports_close() const override { return true; }
    bool       close_source(uint64_t source_key) override;

    std::string open_label() const override { return open_dialog_ ? "Open CSV..." : ""; }
    void        open() override;
    std::string empty_hint() const override;

    // Add (or replace, by path) a parsed file.  Files with a parse error are
    // ignored.  Returns the file id, 0 if ignored.
    uint64_t add_file(const std::string& path, CsvData data);
    // Parse and add a file from disk.
    uint64_t open_file(const std::string& path);

    const std::vector<File>& files() const { return files_; }

    // Column plotted on x for a file: the first column, unless it is the only one.
    static int x_column(const CsvData& data);

   private:
    std::vector<File>     files_;
    uint64_t              next_id_ = 1;
    std::function<void()> open_dialog_;
};

// Hooks the host supplies to install_builtin_providers().
struct BuiltinProviderHooks
{
    logs::LogBrowserPanel* log_panel    = nullptr;
    topics::TopicsPanel*   topics_panel = nullptr;
    std::function<void()>  open_log_dialog;
    std::function<void()>  open_csv_dialog;
};

// Register the providers above (CSV always; logs / topics when their panel is
// given) in sidebar order: logs, CSV, topics.  Returns the CSV provider so the
// host can feed it files loaded elsewhere.
CsvProvider* install_builtin_providers(DataSidebarModel& model, const BuiltinProviderHooks& hooks);

}   // namespace spectra::ui::data_sidebar
