#include "binding_resolver.hpp"

#include <cmath>
#include <filesystem>
#include <spectra/series.hpp>
#include <system_error>

#include "data_binding.hpp"
#include "math/data_transform.hpp"

namespace spectra
{

namespace fs = std::filesystem;

// ─── FileCsvBindingProvider ─────────────────────────────────────────────────

void FileCsvBindingProvider::add_search_dir(const std::string& dir)
{
    if (dir.empty())
        return;
    for (const auto& d : dirs_)
    {
        if (d == dir)
            return;
    }
    dirs_.push_back(dir);
}

void FileCsvBindingProvider::set_file(const std::string& file_name, const std::string& path)
{
    picked_[file_name] = path;
    cache_.erase(path);   // re-read: the user may have picked an updated file
}

std::string FileCsvBindingProvider::locate(const std::string& file_name)
{
    std::error_code ec;
    if (auto it = picked_.find(file_name);
        it != picked_.end() && fs::is_regular_file(it->second, ec))
        return it->second;

    const fs::path name(file_name);
    if (name.is_absolute())
        return fs::is_regular_file(name, ec) ? name.string() : std::string{};

    for (const auto& dir : dirs_)
    {
        const fs::path candidate = fs::path(dir) / name;
        if (fs::is_regular_file(candidate, ec))
            return candidate.string();
    }
    return {};
}

const CsvData& FileCsvBindingProvider::load(const std::string& path)
{
    auto it = cache_.find(path);
    if (it == cache_.end())
        it = cache_.emplace(path, parse_csv(path)).first;
    return it->second;
}

// ─── Transforms ─────────────────────────────────────────────────────────────

void apply_transform_steps(std::span<const LayoutTransformStep> steps,
                           std::vector<float>&                  x,
                           std::vector<float>&                  y)
{
    for (const auto& step : steps)
    {
        if (!step.enabled || step.type < 0 || step.type >= static_cast<int>(TransformType::Custom))
            continue;
        const auto      type = static_cast<TransformType>(step.type);
        TransformParams params;
        switch (type)
        {
            case TransformType::Scale:
                params.scale_factor = step.param;
                break;
            case TransformType::Offset:
                params.offset_value = step.param;
                break;
            case TransformType::Clamp:
                params.clamp_min = -std::fabs(step.param);
                params.clamp_max = std::fabs(step.param);
                break;
            case TransformType::FFT:
                params.fft_sample_rate = step.param;
                break;
            default:
                break;
        }
        std::vector<float> x_out;
        std::vector<float> y_out;
        DataTransform(type, params).apply_y(x, y, x_out, y_out);
        x = std::move(x_out);
        y = std::move(y_out);
    }
}

// ─── BindingResolver ────────────────────────────────────────────────────────

namespace
{

int find_column(const CsvData& data, const std::string& name)
{
    for (size_t c = 0; c < data.headers.size() && c < data.columns.size(); ++c)
    {
        if (data.headers[c] == name)
            return static_cast<int>(c);
    }
    return -1;
}

BindingResult unresolved(std::string msg)
{
    return {BindingStatus::Unresolved, std::move(msg)};
}

}   // namespace

BindingResult BindingResolver::resolve(const std::string&                   uri,
                                       Series&                              series,
                                       FigureId                             figure_id,
                                       uint32_t                             axes_index,
                                       std::span<const LayoutTransformStep> steps) const
{
    std::string error;
    const auto  binding = parse_data_binding(uri, &error);
    if (!binding)
        return unresolved(error);

    switch (binding->scheme)
    {
        case BindingScheme::Topic:
        {
            if (!topics_)
                return unresolved("no topic connection in this window");
            switch (topics_->bind_topic(binding->source, figure_id, axes_index, series))
            {
                case TopicBindingProvider::Result::Subscribed:
                    return {BindingStatus::Bound, "subscribed to " + binding->source};
                case TopicBindingProvider::Result::Pending:
                    return {BindingStatus::Waiting, "waiting for topic " + binding->source};
                case TopicBindingProvider::Result::Unavailable:
                    break;
            }
            return unresolved("could not subscribe to topic " + binding->source);
        }

        case BindingScheme::Csv:
        {
            if (!csv_)
                return unresolved("CSV files cannot be opened here");
            auto* line    = dynamic_cast<LineSeries*>(&series);
            auto* scatter = dynamic_cast<ScatterSeries*>(&series);
            if (!line && !scatter)
                return unresolved("only line and scatter series can take CSV data");

            const std::string path = csv_->locate(binding->source);
            if (path.empty())
                return unresolved("file " + binding->source + " not found");
            const CsvData& data = csv_->load(path);
            if (!data.error.empty())
                return unresolved(binding->source + ": " + data.error);

            const int y_col = find_column(data, binding->field);
            if (y_col < 0)
                return unresolved("no column '" + binding->field + "' in " + binding->source);
            int x_col = -1;
            if (!binding->x_field.empty())
            {
                x_col = find_column(data, binding->x_field);
                if (x_col < 0)
                    return unresolved("no column '" + binding->x_field + "' in " + binding->source);
            }

            auto offset_of = [&](int c)
            {
                return static_cast<size_t>(c) < data.column_offsets.size()
                           ? data.column_offsets[static_cast<size_t>(c)]
                           : 0.0;
            };
            std::vector<float> y = data.columns[static_cast<size_t>(y_col)];
            std::vector<float> x;
            double             x_offset = 0.0;
            if (x_col >= 0)
            {
                x        = data.columns[static_cast<size_t>(x_col)];
                x_offset = offset_of(x_col);
            }
            else
            {
                x.resize(y.size());
                for (size_t i = 0; i < x.size(); ++i)
                    x[i] = static_cast<float>(i);
            }
            double y_offset = offset_of(y_col);
            if (!steps.empty())
            {
                // Transforms work on values: fold the column base in first.
                if (y_offset != 0.0)
                {
                    for (auto& v : y)
                        v = static_cast<float>(y_offset + v);
                    y_offset = 0.0;
                }
                apply_transform_steps(steps, x, y);
            }

            if (line)
                line->set_x(std::span<const float>(x)).set_y(std::span<const float>(y));
            else
                scatter->set_x(std::span<const float>(x)).set_y(std::span<const float>(y));
            series.x_offset(x_offset);
            series.y_offset(y_offset);
            return {BindingStatus::Bound, std::to_string(y.size()) + " points from " + path};
        }

        case BindingScheme::Ulog:
            return unresolved("ULog bindings are not supported yet");
    }
    return unresolved("unknown binding");
}

}   // namespace spectra
