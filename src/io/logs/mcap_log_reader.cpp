#include "mcap_log_reader.hpp"

#include <algorithm>
#include <numeric>
#include <unordered_map>

#ifdef SPECTRA_HAS_MCAP
    #define MCAP_IMPLEMENTATION
    #include <mcap/mcap.hpp>
    #include <nlohmann/json.hpp>

    #include "ros2msg_cdr.hpp"
#endif

namespace spectra::logs
{

bool McapLogReader::has_magic(std::span<const uint8_t> head)
{
    static constexpr uint8_t MAGIC[] = {0x89, 'M', 'C', 'A', 'P', '0', '\r', '\n'};
    return head.size() >= sizeof(MAGIC) && std::equal(MAGIC, MAGIC + sizeof(MAGIC), head.begin());
}

bool McapLogReader::read(size_t field_index, LogSeries& out) const
{
    if (field_index >= columns_.size())
        return fail("Field index out of range");
    out = columns_[field_index];
    return true;
}

#ifndef SPECTRA_HAS_MCAP

bool McapLogReader::open(const std::string& path)
{
    path_ = path;
    fields_.clear();
    columns_.clear();
    return fail("Spectra was built without MCAP support (SPECTRA_USE_MCAP=OFF)");
}

#else

namespace
{

constexpr size_t MAX_WARNINGS = 16;

struct Column
{
    std::string path;
    std::string type;
    LogSeries   data;
};

struct ChannelState
{
    enum class Kind
    {
        Skip,
        Json,
        Ros2Cdr,
    };

    Kind                                    kind = Kind::Skip;
    std::string                             topic;
    ros2msg::Schema                         schema;
    std::vector<Column>                     columns;   // first-seen order
    std::unordered_map<std::string, size_t> by_path;
    size_t                                  decode_failures = 0;
    std::string                             first_failure;

    Column& column(std::string_view path, std::string_view type)
    {
        auto it = by_path.find(std::string(path));
        if (it != by_path.end())
            return columns[it->second];
        by_path.emplace(std::string(path), columns.size());
        columns.push_back(Column{std::string(path), std::string(type), {}});
        return columns.back();
    }

    void append(std::string_view path, std::string_view type, double t, double v)
    {
        Column& c = column(path, type);
        c.data.time.push_back(t);
        c.data.values.push_back(static_cast<float>(v));
    }
};

void flatten_json(const nlohmann::json& j,
                  std::string&          path,
                  size_t                max_array,
                  ChannelState&         state,
                  double                t)
{
    switch (j.type())
    {
        case nlohmann::json::value_t::number_integer:
        case nlohmann::json::value_t::number_unsigned:
        case nlohmann::json::value_t::number_float:
            state.append(path, "number", t, j.get<double>());
            break;
        case nlohmann::json::value_t::boolean:
            state.append(path, "bool", t, j.get<bool>() ? 1.0 : 0.0);
            break;
        case nlohmann::json::value_t::object:
            for (auto it = j.begin(); it != j.end(); ++it)
            {
                const size_t mark = path.size();
                if (!path.empty())
                    path += '.';
                path += it.key();
                flatten_json(it.value(), path, max_array, state, t);
                path.resize(mark);
            }
            break;
        case nlohmann::json::value_t::array:
            if (j.size() > max_array)
                break;
            for (size_t i = 0; i < j.size(); ++i)
            {
                const size_t mark = path.size();
                path += '[' + std::to_string(i) + ']';
                flatten_json(j[i], path, max_array, state, t);
                path.resize(mark);
            }
            break;
        default:   // string, null, binary, discarded
            break;
    }
}

// Base type of a ros2msg field for display: "float64[3]" → "float64".
std::string base_type_name(const ros2msg::FieldDef& f)
{
    auto lb = f.type_name.find('[');
    return lb == std::string::npos ? f.type_name : f.type_name.substr(0, lb);
}

void sort_by_time(LogSeries& s)
{
    if (std::is_sorted(s.time.begin(), s.time.end()))
        return;
    std::vector<size_t> order(s.time.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::stable_sort(order.begin(),
                     order.end(),
                     [&](size_t a, size_t b) { return s.time[a] < s.time[b]; });
    LogSeries sorted;
    sorted.time.reserve(order.size());
    sorted.values.reserve(order.size());
    for (size_t i : order)
    {
        sorted.time.push_back(s.time[i]);
        sorted.values.push_back(s.values[i]);
    }
    s = std::move(sorted);
}

}   // namespace

bool McapLogReader::open(const std::string& path)
{
    path_ = path;
    fields_.clear();
    columns_.clear();
    warnings_.clear();
    error_.clear();

    auto warn = [this](std::string msg)
    {
        if (warnings_.size() < MAX_WARNINGS
            && std::find(warnings_.begin(), warnings_.end(), msg) == warnings_.end())
            warnings_.push_back(std::move(msg));
    };

    mcap::McapReader reader;
    if (auto st = reader.open(path); !st.ok())
        return fail(st.message);

    std::vector<std::string> problems;
    auto                     on_problem = [&](const mcap::Status& st)
    {
        if (problems.size() < 4 * MAX_WARNINGS)
            problems.push_back(st.message);
    };

    // The summary is optional: without one, readMessages() scans the file.
    (void)reader.readSummary(mcap::ReadSummaryMethod::AllowFallbackScan, on_problem);

    std::unordered_map<mcap::ChannelId, ChannelState> channels;
    ros2msg::DecodeOptions                            decode_opts;
    decode_opts.max_array_elements = options_.max_array_elements;

    auto state_for = [&](const mcap::MessageView& mv) -> ChannelState&
    {
        auto [it, inserted] = channels.try_emplace(mv.channel->id);
        ChannelState& s     = it->second;
        if (!inserted)
            return s;

        s.topic                = mv.channel->topic;
        const auto& encoding   = mv.channel->messageEncoding;
        const auto* schema     = mv.schema.get();
        const auto  schema_enc = schema ? schema->encoding : std::string();
        if (encoding == "json")
        {
            s.kind = ChannelState::Kind::Json;
        }
        else if (encoding == "cdr" && schema_enc == "ros2msg")
        {
            std::string      err;
            std::string_view text(reinterpret_cast<const char*>(schema->data.data()),
                                  schema->data.size());
            if (s.schema.parse(schema->name, text, &err))
                s.kind = ChannelState::Kind::Ros2Cdr;
            else
                warn(s.topic + ": cannot parse ros2msg schema " + schema->name + ": " + err);
        }
        else
        {
            std::string what = "message encoding \"" + encoding + "\"";
            if (!schema_enc.empty())
                what += " with schema encoding \"" + schema_enc + "\"";
            warn(s.topic + ": skipped, " + what + " is not supported");
        }
        return s;
    };

    std::string path_buf;
    auto        messages = reader.readMessages(on_problem);
    for (const auto& mv : messages)
    {
        if (!mv.channel)
            continue;
        ChannelState& s = state_for(mv);
        if (s.kind == ChannelState::Kind::Skip)
            continue;

        const double t     = static_cast<double>(mv.message.logTime) * 1e-9;
        const auto*  bytes = mv.message.data;
        const size_t size  = static_cast<size_t>(mv.message.dataSize);

        std::string err;
        bool        ok = true;
        if (s.kind == ChannelState::Kind::Json)
        {
            const auto* begin = reinterpret_cast<const char*>(bytes);
            auto        j     = nlohmann::json::parse(begin, begin + size, nullptr, false);
            if (j.is_discarded())
            {
                ok  = false;
                err = "invalid JSON";
            }
            else
            {
                path_buf.clear();
                flatten_json(j, path_buf, options_.max_array_elements, s, t);
            }
        }
        else
        {
            ok = ros2msg::decode_cdr(
                s.schema,
                std::span<const std::byte>(bytes, size),
                [&](std::string_view p, double v, const ros2msg::FieldDef& f)
                { s.append(p, base_type_name(f), t, v); },
                decode_opts,
                &err);
        }
        if (!ok && s.decode_failures++ == 0)
            s.first_failure = err;
    }
    reader.close();

    for (auto& p : problems)
        warn(p);

    // Topics sorted by name; fields in first-seen order within a topic.
    std::vector<ChannelState*> ordered;
    for (auto& [id, s] : channels)
        ordered.push_back(&s);
    std::sort(ordered.begin(),
              ordered.end(),
              [](const ChannelState* a, const ChannelState* b) { return a->topic < b->topic; });

    for (ChannelState* s : ordered)
    {
        if (s->decode_failures > 0)
            warn(s->topic + ": " + std::to_string(s->decode_failures)
                 + " message(s) could not be fully decoded (" + s->first_failure + ")");
        for (auto& c : s->columns)
        {
            sort_by_time(c.data);
            LogField f;
            f.topic        = s->topic;
            f.path         = c.path;
            f.type         = c.type;
            f.sample_count = c.data.time.size();
            fields_.push_back(std::move(f));
            columns_.push_back(std::move(c.data));
        }
    }

    if (fields_.empty() && !problems.empty())
        return fail(problems.front());
    return true;
}

#endif   // SPECTRA_HAS_MCAP

}   // namespace spectra::logs
