#pragma once

// launch_files.hpp — routing of files the user hands to Spectra: OS
// drag-and-drop, `spectra file.csv ...` on the command line, File > Open File...
// and the start page's recent list all go through classify_open_file().

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace spectra
{

// How a file is opened.
enum class OpenFileKind
{
    Csv,           // .csv / .tsv / .txt -> CSV column picker
    Log,           // .ulg / .mcap (whatever the log reader registry opens) -> log browser
    Layout,        // .spectra / .json -> File > Open Layout
    Unsupported,   // anything else
};

// Lower-case extension without the dot ("" when there is none).
std::string open_file_extension(std::string_view path);

// Classifies @p path by its extension.  @p is_log, when set, decides whether a
// file that is neither CSV nor a layout is a log the log browser can open (the
// log reader registry); without it, .ulg and .mcap count as logs.
OpenFileKind classify_open_file(std::string_view                               path,
                                const std::function<bool(const std::string&)>& is_log = {});

// Short tag for the start page's recent list ("CSV", "TSV", "ULog", "MCAP",
// "Layout"); the upper-case extension otherwise.
std::string open_file_kind_label(std::string_view path);

// ─── Command line ────────────────────────────────────────────────────────────

struct LaunchArgs
{
    std::string              layout;     // --layout FILE
    std::vector<std::string> files;      // positional paths, in order
    std::vector<std::string> warnings;   // unknown options, one line each
    std::string              error;      // non-empty: print it and exit with code 2
    bool                     show_help    = false;
    bool                     show_version = false;
};

// Parses `spectra [OPTIONS] [FILE...]`.  Recognized options: --layout FILE
// (also --layout=FILE), --version/-v, --help/-h and --no-native-dialogs (read
// by init_native_dialog_policy(), accepted here).  Everything after "--" is a
// file.  Unknown options produce a warning and are otherwise ignored.
// Positional paths are kept as given; the caller checks they exist and routes
// them with classify_open_file().
LaunchArgs parse_launch_args(int argc, const char* const* argv);

// Usage text printed by --help.
std::string launch_usage();

}   // namespace spectra
