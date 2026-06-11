#include "session_store.hpp"

#include "figure_model.hpp"
#include "session_graph.hpp"

#include "../ipc/codec.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef SPECTRA_USE_POSTGRES
    #include <libpq-fe.h>
#endif

namespace spectra::daemon
{

namespace
{

std::string escape_json(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s)
    {
        switch (c)
        {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += c;
                break;
        }
    }
    return out;
}

std::string encode_graph_json(const SessionGraphSnapshot& snap)
{
    std::ostringstream os;
    os << "{\"figures\":[";
    for (size_t i = 0; i < snap.figures.size(); ++i)
    {
        if (i > 0)
            os << ',';
        const auto& f = snap.figures[i];
        os << "{\"id\":" << f.figure_id << ",\"window\":" << f.assigned_window << ",\"title\":\""
           << escape_json(f.title) << "\"}";
    }
    os << "],\"pending_windows\":[";
    for (size_t i = 0; i < snap.pending_windows.size(); ++i)
    {
        if (i > 0)
            os << ',';
        const auto& w = snap.pending_windows[i];
        os << "{\"id\":" << w.window_id << ",\"figures\":[";
        for (size_t j = 0; j < w.assigned_figures.size(); ++j)
        {
            if (j > 0)
                os << ',';
            os << w.assigned_figures[j];
        }
        os << "]}";
    }
    os << "]}";
    return os.str();
}

bool parse_uint64_after(const std::string& json, size_t key_pos, uint64_t& out)
{
    size_t colon = json.find(':', key_pos);
    if (colon == std::string::npos)
        return false;
    size_t start = json.find_first_not_of(" \t\n\r", colon + 1);
    if (start == std::string::npos)
        return false;
    size_t end = start;
    while (end < json.size() && json[end] >= '0' && json[end] <= '9')
        ++end;
    if (end == start)
        return false;
    out = std::stoull(json.substr(start, end - start));
    return true;
}

std::string read_json_string_at(const std::string& json, size_t key_pos)
{
    size_t colon = json.find(':', key_pos);
    if (colon == std::string::npos)
        return {};
    size_t quote = json.find('"', colon + 1);
    if (quote == std::string::npos)
        return {};
    size_t end = quote + 1;
    while (end < json.size())
    {
        if (json[end] == '"' && json[end - 1] != '\\')
            break;
        ++end;
    }
    return json.substr(quote + 1, end - quote - 1);
}

bool decode_graph_json(const std::string& json, SessionGraphSnapshot& snap)
{
    snap.figures.clear();
    snap.pending_windows.clear();

    size_t figures_pos = json.find("\"figures\"");
    if (figures_pos != std::string::npos)
    {
        size_t search = figures_pos;
        while ((search = json.find("\"id\":", search + 1)) != std::string::npos)
        {
            if (search > json.find("\"pending_windows\"", figures_pos))
                break;
            SessionGraphSnapshot::FigureRow row;
            if (!parse_uint64_after(json, search, row.figure_id))
                continue;
            size_t window_key = json.find("\"window\":", search);
            if (window_key == std::string::npos || window_key > search + 80)
                continue;
            uint64_t window = 0;
            if (!parse_uint64_after(json, window_key, window))
                continue;
            row.assigned_window = window;
            size_t title_key = json.find("\"title\":", search);
            if (title_key != std::string::npos && title_key < search + 120)
                row.title = read_json_string_at(json, title_key);
            snap.figures.push_back(std::move(row));
        }
    }

    size_t pending_pos = json.find("\"pending_windows\"");
    if (pending_pos != std::string::npos)
    {
        size_t search = pending_pos;
        while ((search = json.find("\"id\":", search + 1)) != std::string::npos)
        {
            SessionGraphSnapshot::PendingWindowRow row;
            if (!parse_uint64_after(json, search, row.window_id))
                continue;
            size_t figures_key = json.find("\"figures\":", search);
            if (figures_key == std::string::npos || figures_key > search + 40)
                continue;
            size_t bracket = json.find('[', figures_key);
            size_t close   = json.find(']', bracket);
            if (bracket == std::string::npos || close == std::string::npos)
                continue;
            size_t cursor = bracket + 1;
            while (cursor < close)
            {
                size_t digit = json.find_first_of("0123456789", cursor);
                if (digit == std::string::npos || digit >= close)
                    break;
                size_t end = digit;
                while (end < close && json[end] >= '0' && json[end] <= '9')
                    ++end;
                row.assigned_figures.push_back(
                    std::stoull(json.substr(digit, end - digit)));
                cursor = end + 1;
            }
            snap.pending_windows.push_back(std::move(row));
        }
    }

    return true;
}

}   // namespace

#ifdef SPECTRA_USE_POSTGRES

struct SessionStore::PgConn
{
    PGconn* handle = nullptr;
};

SessionStore::SessionStore() : conn_(new PgConn()) {}

SessionStore::~SessionStore()
{
    if (conn_ && conn_->handle)
        PQfinish(conn_->handle);
    delete conn_;
}

bool SessionStore::init()
{
    if (conn_->handle)
        return enabled_;

    const char* url = std::getenv("DATABASE_URL");
    if (!url || url[0] == '\0')
        return false;

    conn_->handle = PQconnectdb(url);
    if (PQstatus(conn_->handle) != CONNECTION_OK)
    {
        std::cerr << "[spectra-backend] Postgres connect failed: " << PQerrorMessage(conn_->handle)
                  << "\n";
        PQfinish(conn_->handle);
        conn_->handle = nullptr;
        return false;
    }

    enabled_ = true;
    std::cerr << "[spectra-backend] Postgres session persistence enabled\n";
    return true;
}

bool SessionStore::try_load(SessionGraph& graph, FigureModel& model)
{
    if (!enabled_ || !conn_->handle)
        return false;

    PGresult* res = PQexec(conn_->handle,
                           "SELECT session_id, next_window_id, next_figure_id, graph_state::text, "
                           "figure_snapshot, figure_revision "
                           "FROM daemon_session_state "
                           "ORDER BY updated_at DESC LIMIT 1");
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return false;
    }

    SessionGraphSnapshot snap;
    snap.session_id     = std::stoull(PQgetvalue(res, 0, 0));
    snap.next_window_id = std::stoull(PQgetvalue(res, 0, 1));
    snap.next_figure_id = std::stoull(PQgetvalue(res, 0, 2));
    const char* graph_json = PQgetvalue(res, 0, 3);
    if (graph_json)
        decode_graph_json(graph_json, snap);

    const char* figure_bytes = PQgetvalue(res, 0, 4);
    const int   figure_len   = PQgetlength(res, 0, 4);
    std::vector<uint8_t> figure_blob;
    if (figure_bytes && figure_len > 0)
        figure_blob.assign(reinterpret_cast<const uint8_t*>(figure_bytes),
                           reinterpret_cast<const uint8_t*>(figure_bytes) + figure_len);

    PQclear(res);

    graph.import_snapshot(snap);

    if (!figure_blob.empty())
    {
        auto decoded = ipc::decode_state_snapshot(figure_blob);
        if (decoded)
            model.load_snapshot(*decoded);
    }

    return true;
}

bool SessionStore::save(const SessionGraph& graph, const FigureModel& model)
{
    if (!enabled_ || !conn_->handle)
        return false;

    SessionGraphSnapshot snap;
    graph.export_snapshot(snap);
    const std::string graph_json = encode_graph_json(snap);
    const auto        fig_snap   = model.snapshot();
    const auto        fig_blob   = ipc::encode_state_snapshot(fig_snap);

    const char* param_values[6];
    std::string session_id_str     = std::to_string(snap.session_id);
    std::string next_window_id_str  = std::to_string(snap.next_window_id);
    std::string next_figure_id_str  = std::to_string(snap.next_figure_id);
    std::string figure_revision_str = std::to_string(fig_snap.revision);
    param_values[0]                 = session_id_str.c_str();
    param_values[1]                 = next_window_id_str.c_str();
    param_values[2]                 = next_figure_id_str.c_str();
    param_values[3]                 = graph_json.c_str();
    param_values[4]                 = reinterpret_cast<const char*>(fig_blob.data());
    param_values[5]                 = figure_revision_str.c_str();

    int param_lengths[6] = {0, 0, 0, static_cast<int>(graph_json.size()),
                            static_cast<int>(fig_blob.size()), 0};
    int param_formats[6] = {0, 0, 0, 0, 1, 0};

    PGresult* res = PQexecParams(conn_->handle,
                                 "INSERT INTO daemon_session_state "
                                 "(session_id, next_window_id, next_figure_id, graph_state, "
                                 "figure_snapshot, figure_revision) "
                                 "VALUES ($1::bigint, $2::bigint, $3::bigint, $4::jsonb, $5, "
                                 "$6::bigint) "
                                 "ON CONFLICT (session_id) DO UPDATE SET "
                                 "next_window_id = EXCLUDED.next_window_id, "
                                 "next_figure_id = EXCLUDED.next_figure_id, "
                                 "graph_state = EXCLUDED.graph_state, "
                                 "figure_snapshot = EXCLUDED.figure_snapshot, "
                                 "figure_revision = EXCLUDED.figure_revision, "
                                 "updated_at = now()",
                                 6,
                                 nullptr,
                                 param_values,
                                 param_lengths,
                                 param_formats,
                                 0);

    const bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    if (!ok)
    {
        std::cerr << "[spectra-backend] Postgres session save failed: "
                  << PQerrorMessage(conn_->handle) << "\n";
    }
    PQclear(res);
    return ok;
}

#else

SessionStore::SessionStore()  = default;
SessionStore::~SessionStore() = default;

bool SessionStore::init()
{
    return false;
}

bool SessionStore::try_load(SessionGraph& /*graph*/, FigureModel& /*model*/)
{
    return false;
}

bool SessionStore::save(const SessionGraph& /*graph*/, const FigureModel& /*model*/)
{
    return false;
}

#endif

}   // namespace spectra::daemon
