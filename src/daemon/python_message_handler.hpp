#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "daemon_server.hpp"

namespace spectra::daemon
{

// Reassembles chunked REQ_SET_DATA transfers (python/spectra/_series.py
// _send_chunked). Pure logic, no I/O. Chunks for one (client, figure, series)
// must arrive in order starting at chunk_index 0; a chunk_index 0 restarts the
// assembly. Memory is bounded per assembly and across all pending assemblies.
class SetDataChunkAssembler
{
   public:
    enum class Status
    {
        Complete,   // `data` holds the full array: apply it.
        Pending,    // chunk buffered, more expected: reply OK.
        Error,      // `error` describes the problem; pending state was dropped.
    };

    struct Result
    {
        Status             status = Status::Error;
        std::vector<float> data;
        std::string        error;
    };

    // Default limits: 1 GiB of floats per assembly, 2 GiB pending in total.
    static constexpr size_t DEFAULT_MAX_FLOATS_PER_ASSEMBLY = 256u * 1024u * 1024u;
    static constexpr size_t DEFAULT_MAX_PENDING_FLOATS      = 512u * 1024u * 1024u;

    SetDataChunkAssembler() = default;
    SetDataChunkAssembler(size_t max_floats_per_assembly, size_t max_pending_floats)
        : max_floats_per_assembly_(max_floats_per_assembly), max_pending_floats_(max_pending_floats)
    {
    }

    // Feed one decoded REQ_SET_DATA. `req.data` is consumed. An unchunked request
    // completes immediately and discards any half-built assembly for the same key.
    Result add(uint64_t client_id, ipc::ReqSetDataPayload&& req);

    // Drop every pending assembly that belongs to `client_id`.
    void drop_client(uint64_t client_id);

    // Drop pending assemblies whose client id is not in `live_client_ids`.
    void retain_clients(const std::vector<uint64_t>& live_client_ids);

    size_t pending_count() const { return pending_.size(); }
    size_t pending_floats() const { return pending_floats_; }

   private:
    using Key = std::tuple<uint64_t, uint64_t, uint32_t>;   // client, figure, series

    struct Pending
    {
        uint32_t           chunk_count = 0;
        uint32_t           next_index  = 0;
        uint32_t           total_count = 0;
        std::vector<float> data;
    };

    void   erase(std::map<Key, Pending>::iterator it);
    Result fail(const Key& key, std::string message);

    size_t                 max_floats_per_assembly_ = DEFAULT_MAX_FLOATS_PER_ASSEMBLY;
    size_t                 max_pending_floats_      = DEFAULT_MAX_PENDING_FLOATS;
    size_t                 pending_floats_          = 0;   // sum of reserved total_count
    std::map<Key, Pending> pending_;
};

// Handles Python client requests (REQ_CREATE_FIGURE, REQ_ADD_SERIES, REQ_SET_DATA,
// REQ_UPDATE_PROPERTY, REQ_SHOW, REQ_APPEND_DATA, REQ_REMOVE_SERIES, REQ_CLOSE_FIGURE,
// REQ_UPDATE_BATCH, REQ_DESTROY_FIGURE, REQ_LIST_FIGURES, REQ_RECONNECT, REQ_DISCONNECT,
// REQ_GET_SNAPSHOT, REQ_CREATE_AXES).

// ─── SetDataChunkAssembler (inline: header-only so unit tests can use it) ───

inline void SetDataChunkAssembler::erase(std::map<Key, Pending>::iterator it)
{
    pending_floats_ -= std::min<size_t>(pending_floats_, it->second.total_count);
    pending_.erase(it);
}

inline SetDataChunkAssembler::Result SetDataChunkAssembler::fail(const Key&  key,
                                                                 std::string message)
{
    auto it = pending_.find(key);
    if (it != pending_.end())
        erase(it);
    Result r;
    r.status = Status::Error;
    r.error  = std::move(message);
    return r;
}

inline SetDataChunkAssembler::Result SetDataChunkAssembler::add(uint64_t                 client_id,
                                                                ipc::ReqSetDataPayload&& req)
{
    const Key key{client_id, req.figure_id, req.series_index};

    if (!req.is_chunked())
    {
        if (req.chunk_index != 0)
            return fail(key, "Invalid chunk index for unchunked set_data");
        // A complete set_data supersedes any half-built chunked transfer.
        auto it = pending_.find(key);
        if (it != pending_.end())
            erase(it);
        Result r;
        r.status = Status::Complete;
        r.data   = std::move(req.data);
        return r;
    }

    if (req.chunk_index >= req.chunk_count)
        return fail(key, "Chunk index out of range");

    auto it = pending_.find(key);
    if (req.chunk_index == 0)
    {
        // chunk 0 (re)starts the transfer.
        if (it != pending_.end())
            erase(it);
        if (req.total_count == 0)
            return fail(key, "Chunked set_data with zero total_count");
        if (req.total_count > max_floats_per_assembly_)
            return fail(key, "Chunked set_data exceeds maximum size");
        if (pending_floats_ + req.total_count > max_pending_floats_)
            return fail(key, "Too much chunked set_data pending");

        Pending p;
        p.chunk_count = req.chunk_count;
        p.total_count = req.total_count;
        p.data.reserve(req.total_count);
        it = pending_.emplace(key, std::move(p)).first;
        pending_floats_ += req.total_count;
    }
    else
    {
        if (it == pending_.end())
            return fail(key, "Chunk received without chunk 0");
        const Pending& p = it->second;
        if (req.chunk_count != p.chunk_count || req.total_count != p.total_count)
            return fail(key, "Chunk metadata does not match transfer");
        if (req.chunk_index != p.next_index)
            return fail(key, "Out-of-order chunk");
    }

    Pending& p = it->second;
    if (req.data.size() > static_cast<size_t>(p.total_count) - p.data.size())
        return fail(key, "Chunked set_data exceeds declared total_count");
    p.data.insert(p.data.end(), req.data.begin(), req.data.end());
    ++p.next_index;

    if (p.next_index < p.chunk_count)
    {
        Result r;
        r.status = Status::Pending;
        return r;
    }

    if (p.data.size() != p.total_count)
        return fail(key, "Chunked set_data shorter than declared total_count");

    Result r;
    r.status = Status::Complete;
    r.data   = std::move(p.data);
    erase(it);
    return r;
}

inline void SetDataChunkAssembler::drop_client(uint64_t client_id)
{
    for (auto it = pending_.begin(); it != pending_.end();)
    {
        auto next = std::next(it);
        if (std::get<0>(it->first) == client_id)
            erase(it);
        it = next;
    }
}

inline void SetDataChunkAssembler::retain_clients(const std::vector<uint64_t>& live_client_ids)
{
    for (auto it = pending_.begin(); it != pending_.end();)
    {
        auto       next = std::next(it);
        const auto id   = std::get<0>(it->first);
        if (std::find(live_client_ids.begin(), live_client_ids.end(), id) == live_client_ids.end())
            erase(it);
        it = next;
    }
}

HandleResult handle_req_create_figure(DaemonContext&      ctx,
                                      ClientSlot&         slot,
                                      const ipc::Message& msg);

HandleResult handle_req_create_axes(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg);

HandleResult handle_req_add_series(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg);

HandleResult handle_req_set_data(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg);

HandleResult handle_req_update_property(DaemonContext&      ctx,
                                        ClientSlot&         slot,
                                        const ipc::Message& msg);

HandleResult handle_req_show(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg);

HandleResult handle_req_append_data(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg);

HandleResult handle_req_remove_series(DaemonContext&      ctx,
                                      ClientSlot&         slot,
                                      const ipc::Message& msg);

HandleResult handle_req_close_figure(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg);

HandleResult handle_req_update_batch(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg);

HandleResult handle_req_destroy_figure(DaemonContext&      ctx,
                                       ClientSlot&         slot,
                                       const ipc::Message& msg);

HandleResult handle_req_list_figures(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg);

HandleResult handle_req_reconnect(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg);

HandleResult handle_req_disconnect(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg);

HandleResult handle_req_get_snapshot(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg);

HandleResult handle_req_anim_start(DaemonContext& ctx, ClientSlot& slot, const ipc::Message& msg);

}   // namespace spectra::daemon
