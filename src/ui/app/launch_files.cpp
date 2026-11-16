// launch_files.cpp — classify files handed to Spectra and parse the command line.

#include "launch_files.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>

namespace spectra
{

std::string open_file_extension(std::string_view path)
{
    std::string ext = std::filesystem::path(std::string(path)).extension().string();
    if (!ext.empty() && ext.front() == '.')
        ext.erase(ext.begin());
    std::transform(ext.begin(),
                   ext.end(),
                   ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

OpenFileKind classify_open_file(std::string_view                               path,
                                const std::function<bool(const std::string&)>& is_log)
{
    const std::string ext = open_file_extension(path);
    if (ext == "csv" || ext == "tsv" || ext == "txt")
        return OpenFileKind::Csv;
    if (ext == "spectra" || ext == "json")
        return OpenFileKind::Layout;
    if (is_log)
        return is_log(std::string(path)) ? OpenFileKind::Log : OpenFileKind::Unsupported;
    if (ext == "ulg" || ext == "mcap")
        return OpenFileKind::Log;
    return OpenFileKind::Unsupported;
}

std::string open_file_kind_label(std::string_view path)
{
    const std::string ext = open_file_extension(path);
    if (ext == "ulg")
        return "ULog";
    if (ext == "spectra" || ext == "json")
        return "Layout";
    std::string label = ext;
    std::transform(label.begin(),
                   label.end(),
                   label.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return label;
}

LaunchArgs parse_launch_args(int argc, const char* const* argv)
{
    LaunchArgs out;
    bool       only_files = false;
    for (int i = 1; i < argc; ++i)
    {
        const char* arg = argv[i];
        if (!arg || arg[0] == '\0')
            continue;
        if (only_files || arg[0] != '-')
        {
            out.files.emplace_back(arg);
            continue;
        }
        if (std::strcmp(arg, "--") == 0)
        {
            only_files = true;
            continue;
        }
        if (std::strcmp(arg, "--layout") == 0)
        {
            if (i + 1 >= argc || !argv[i + 1] || argv[i + 1][0] == '\0')
            {
                out.error = "--layout needs a file name";
                return out;
            }
            out.layout = argv[++i];
            continue;
        }
        if (std::strncmp(arg, "--layout=", 9) == 0)
        {
            out.layout = arg + 9;
            if (out.layout.empty())
            {
                out.error = "--layout needs a file name";
                return out;
            }
            continue;
        }
        if (std::strcmp(arg, "--version") == 0 || std::strcmp(arg, "-v") == 0)
        {
            out.show_version = true;
            continue;
        }
        if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0)
        {
            out.show_help = true;
            continue;
        }
        if (std::strcmp(arg, "--no-native-dialogs") == 0)
            continue;
        out.warnings.push_back(std::string("ignoring unknown option '") + arg
                               + "' (see spectra --help)");
    }
    return out;
}

std::string launch_usage()
{
    return "Usage: spectra [OPTIONS] [FILE...]\n"
           "\n"
           "GPU-accelerated scientific plotting application.\n"
           "\n"
           "Files are opened as if dropped on the window: CSV/TSV files open the\n"
           "column picker, ULog (.ulg) and MCAP (.mcap) logs open in the log browser,\n"
           "and .spectra layouts open like File > Open Layout.\n"
           "\n"
           "Options:\n"
           "  --layout FILE    Open a layout file (File > Save Layout) and\n"
           "                   re-resolve its data bindings (topics, CSV files).\n"
           "  --no-native-dialogs\n"
           "                   Suppress native OS file dialogs (automation / fuzz).\n"
           "  --version, -v    Print version and exit\n"
           "  --help, -h       Show this help\n"
           "  --               Treat every following argument as a file\n"
           "\n"
           "Environment:\n"
           "  SPECTRA_NO_DAEMON_DISCOVERY=1   Stay in-process even if a\n"
           "                                  Spectra daemon is running.\n"
           "  SPECTRA_NO_NATIVE_DIALOGS=1     Suppress native file dialogs.\n"
           "  SPECTRA_AUTOMATION=1            Alias for SPECTRA_NO_NATIVE_DIALOGS.\n";
}

}   // namespace spectra
