#pragma once

// Px4RecentFiles — most-recently-opened ULog files for spectra-px4.
//
// Persisted as a small JSON file in the same config directory the main
// Spectra app uses for its settings (~/.config/spectra/), so the list
// survives restarts.  Newest entry first, at most max_entries() entries,
// each path stored once (absolute, normalised).
//
// File format:
//   { "version": 1, "files": ["/abs/path/a.ulg", "/abs/path/b.ulg"] }

#include <cstddef>
#include <string>
#include <vector>

namespace spectra::adapters::px4
{

class Px4RecentFiles
{
   public:
    static constexpr size_t kDefaultMaxEntries = 10;

    // An empty path keeps the list in memory only (nothing is read or written).
    explicit Px4RecentFiles(std::string path = {}, size_t max_entries = kDefaultMaxEntries);

    // <spectra config dir>/px4_recent.json (the directory that holds settings.json).
    static std::string default_path();

    const std::string& path() const { return path_; }
    void               set_path(std::string path) { path_ = std::move(path); }
    size_t             max_entries() const { return max_entries_; }

    // Replace the in-memory list with the file's contents.  Returns false (and
    // leaves the list empty) when the file is missing or unreadable.
    bool load();
    // Write the list to path().  True when the path is empty (in-memory store).
    bool save() const;

    // Move `file` to the front (adding it if new), trim to max_entries(), save.
    void push(const std::string& file);
    // Drop `file` from the list (if present) and save.
    void remove(const std::string& file);
    // Empty the list and save.
    void clear();

    const std::vector<std::string>& entries() const { return entries_; }
    bool                            empty() const { return entries_.empty(); }

    static std::string              normalize(const std::string& file);
    static std::string              serialize(const std::vector<std::string>& entries);
    static std::vector<std::string> deserialize(const std::string& json);

   private:
    std::string              path_;
    size_t                   max_entries_;
    std::vector<std::string> entries_;
};

}   // namespace spectra::adapters::px4
