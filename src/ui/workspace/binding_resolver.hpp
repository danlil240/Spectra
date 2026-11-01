#pragma once

// binding_resolver.hpp — connects the data bindings of a layout file to data.
//
// When a layout is opened, every series is created empty and its binding URI
// (see data_binding.hpp) is resolved here:
//   topic://  through a TopicBindingProvider (the in-process topic server),
//             which subscribes now or queues a pending subscription that is
//             promoted when the publisher declares the topic;
//   csv://    through a CsvBindingProvider, which finds the file by name and
//             parses it; the saved per-axes transform steps are applied;
//   ulog://   reserved, reported as unresolved for now.
// The providers are interfaces so tests can use fakes.

#include <span>
#include <spectra/fwd.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "ui/data/csv_loader.hpp"
#include "workspace.hpp"

namespace spectra
{

class Series;

using LayoutTransformStep = WorkspaceData::TransformState::Step;

// Feeds a live topic into an existing series.
class TopicBindingProvider
{
   public:
    enum class Result
    {
        Subscribed,    // topic exists: samples flow now
        Pending,       // topic not declared yet: subscribed once it is
        Unavailable,   // series / figure unknown, or no topic transport
    };

    virtual ~TopicBindingProvider() = default;

    // Feed samples of @p topic into @p series, which lives on axes
    // @p axes_index of figure @p figure_id.  Main thread only.
    virtual Result bind_topic(const std::string& topic,
                              FigureId           figure_id,
                              uint32_t           axes_index,
                              Series&            series) = 0;
};

// Finds and parses CSV files named by csv:// bindings.
class CsvBindingProvider
{
   public:
    virtual ~CsvBindingProvider() = default;

    // Full path of the file a binding names, or "" if it cannot be found.
    virtual std::string locate(const std::string& file_name) = 0;

    // Parsed contents of @p path (CsvData::error is set on failure).  The
    // reference stays valid until the provider is destroyed or its cache cleared.
    virtual const CsvData& load(const std::string& path) = 0;
};

// CsvBindingProvider over the file system.  A file name is looked up in this
// order: a file the user picked for that name (set_file()), then each search
// directory (typically the layout file's directory and the working
// directory).  Absolute names are used as they are.  Parsed files are cached.
class FileCsvBindingProvider final : public CsvBindingProvider
{
   public:
    void                            add_search_dir(const std::string& dir);
    const std::vector<std::string>& search_dirs() const { return dirs_; }

    // Use @p path for bindings naming @p file_name (e.g. picked in a dialog).
    void set_file(const std::string& file_name, const std::string& path);

    std::string    locate(const std::string& file_name) override;
    const CsvData& load(const std::string& path) override;

    void clear_cache() { cache_.clear(); }

   private:
    std::vector<std::string>                     dirs_;
    std::unordered_map<std::string, std::string> picked_;
    std::unordered_map<std::string, CsvData>     cache_;
};

enum class BindingStatus
{
    Bound,        // data loaded / topic subscribed
    Waiting,      // topic subscription pending until the publisher appears
    Unresolved,   // nothing to bind to: see the message
};

struct BindingResult
{
    BindingStatus status = BindingStatus::Unresolved;
    std::string   message;   // reason when unresolved, details otherwise
};

class BindingResolver
{
   public:
    // Either provider may be null: bindings of that kind are then unresolved.
    BindingResolver(TopicBindingProvider* topics, CsvBindingProvider* csv)
        : topics_(topics), csv_(csv)
    {
    }

    // Resolve @p uri into @p series (on axes @p axes_index of figure
    // @p figure_id).  @p steps is the axes' saved transform pipeline, applied
    // to file data (not yet to live topics).
    BindingResult resolve(const std::string&                   uri,
                          Series&                              series,
                          FigureId                             figure_id,
                          uint32_t                             axes_index,
                          std::span<const LayoutTransformStep> steps = {}) const;

   private:
    TopicBindingProvider* topics_ = nullptr;
    CsvBindingProvider*   csv_    = nullptr;
};

// Applies saved transform steps (DataTransform types; disabled steps and
// unknown / custom types are skipped) to (x, y).  The step parameter is the
// Scale factor, Offset value, Clamp bound (clamps to [-|param|, |param|]) or
// FFT sample rate.
void apply_transform_steps(std::span<const LayoutTransformStep> steps,
                           std::vector<float>&                  x,
                           std::vector<float>&                  y);

}   // namespace spectra
