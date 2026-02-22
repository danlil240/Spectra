#include "csv_loader.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace spectra
{

namespace
{

// Detect delimiter by scanning the first line.
char detect_delimiter(const std::string& line)
{
    int commas = 0, semicolons = 0, tabs = 0;
    for (char c : line)
    {
        if (c == ',')
            ++commas;
        else if (c == ';')
            ++semicolons;
        else if (c == '\t')
            ++tabs;
    }
    if (tabs >= commas && tabs >= semicolons)
        return '\t';
    if (semicolons > commas)
        return ';';
    return ',';
}

// Try to parse a string as a float. Returns true on success.
bool try_parse_float(const std::string& s, float& out)
{
    if (s.empty())
        return false;
    char* end = nullptr;
    float val = std::strtof(s.c_str(), &end);
    // Must consume the entire string (ignoring trailing whitespace)
    while (end && *end && std::isspace(static_cast<unsigned char>(*end)))
        ++end;
    if (end == s.c_str() || (end && *end != '\0'))
        return false;
    out = val;
    return true;
}

// Split a line by delimiter, respecting quoted fields.
std::vector<std::string> split_line(const std::string& line, char delim)
{
    std::vector<std::string> fields;
    std::string field;
    bool in_quotes = false;

    for (size_t i = 0; i < line.size(); ++i)
    {
        char c = line[i];
        if (c == '"')
        {
            in_quotes = !in_quotes;
        }
        else if (c == delim && !in_quotes)
        {
            // Trim whitespace
            while (!field.empty()
                   && std::isspace(static_cast<unsigned char>(field.front())))
                field.erase(field.begin());
            while (!field.empty()
                   && std::isspace(static_cast<unsigned char>(field.back())))
                field.pop_back();
            fields.push_back(field);
            field.clear();
        }
        else
        {
            field += c;
        }
    }
    // Last field
    while (!field.empty() && std::isspace(static_cast<unsigned char>(field.front())))
        field.erase(field.begin());
    while (!field.empty() && std::isspace(static_cast<unsigned char>(field.back())))
        field.pop_back();
    fields.push_back(field);

    return fields;
}

}  // namespace

CsvData parse_csv(const std::string& path)
{
    CsvData result;

    std::ifstream file(path);
    if (!file.is_open())
    {
        result.error = "Cannot open file: " + path;
        return result;
    }

    // Read all lines
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line))
    {
        // Strip trailing \r (Windows line endings)
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!line.empty())
            lines.push_back(line);
    }

    if (lines.empty())
    {
        result.error = "File is empty";
        return result;
    }

    // Detect delimiter from first line
    char delim = detect_delimiter(lines[0]);

    // Parse first line to detect if it's a header
    auto first_fields = split_line(lines[0], delim);
    result.num_cols = first_fields.size();

    if (result.num_cols == 0)
    {
        result.error = "No columns detected";
        return result;
    }

    // Check if first row is a header (at least one non-numeric field)
    bool has_header = false;
    for (const auto& f : first_fields)
    {
        float dummy;
        if (!try_parse_float(f, dummy))
        {
            has_header = true;
            break;
        }
    }

    size_t data_start = 0;
    if (has_header)
    {
        result.headers = first_fields;
        data_start = 1;
    }
    else
    {
        // Generate default column names
        for (size_t i = 0; i < result.num_cols; ++i)
            result.headers.push_back("Column " + std::to_string(i + 1));
    }

    // Initialize columns
    result.columns.resize(result.num_cols);

    // Parse data rows
    for (size_t i = data_start; i < lines.size(); ++i)
    {
        auto fields = split_line(lines[i], delim);
        for (size_t c = 0; c < result.num_cols; ++c)
        {
            float val = 0.0f;
            if (c < fields.size())
                try_parse_float(fields[c], val);
            result.columns[c].push_back(val);
        }
    }

    result.num_rows = lines.size() - data_start;
    return result;
}

std::vector<std::string> list_csv_files(const std::string& directory)
{
    std::vector<std::string> files;
    try
    {
        for (const auto& entry : std::filesystem::directory_iterator(directory))
        {
            if (!entry.is_regular_file())
                continue;
            auto ext = entry.path().extension().string();
            // Case-insensitive .csv check
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (ext == ".csv" || ext == ".tsv" || ext == ".txt")
                files.push_back(entry.path().string());
        }
        std::sort(files.begin(), files.end());
    }
    catch (const std::exception&)
    {
        // Directory not accessible — return empty
    }
    return files;
}

}  // namespace spectra
