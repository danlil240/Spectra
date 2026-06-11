#include "session_graph.hpp"

#include <algorithm>

namespace spectra::daemon
{

void SessionGraph::bump_mutation()
{
    // Caller must hold mu_.
    ++mutation_gen_;
}

uint64_t SessionGraph::mutation_generation() const
{
    std::lock_guard lock(mu_);
    return mutation_gen_;
}

void SessionGraph::export_snapshot(SessionGraphSnapshot& out) const
{
    std::lock_guard lock(mu_);
    out.session_id     = session_id_;
    out.next_window_id = next_window_id_;
    out.next_figure_id = next_figure_id_;
    out.figures.clear();
    out.pending_windows.clear();

    out.figures.reserve(figures_.size());
    for (const auto& [fid, entry] : figures_)
    {
        SessionGraphSnapshot::FigureRow row;
        row.figure_id       = fid;
        row.assigned_window = entry.assigned_window;
        row.title           = entry.title;
        out.figures.push_back(std::move(row));
    }

    for (const auto& [wid, agent] : agents_)
    {
        if (agent.connection_fd != -1)
            continue;
        SessionGraphSnapshot::PendingWindowRow row;
        row.window_id         = wid;
        row.assigned_figures  = agent.assigned_figures;
        out.pending_windows.push_back(std::move(row));
    }
}

void SessionGraph::import_snapshot(const SessionGraphSnapshot& snap)
{
    std::lock_guard lock(mu_);
    session_id_     = snap.session_id;
    next_window_id_ = snap.next_window_id;
    next_figure_id_ = snap.next_figure_id;
    agents_.clear();
    figures_.clear();

    for (const auto& row : snap.figures)
    {
        FigureEntry entry;
        entry.figure_id       = row.figure_id;
        entry.assigned_window = row.assigned_window;
        entry.title           = row.title;
        figures_[row.figure_id] = std::move(entry);
    }

    for (const auto& row : snap.pending_windows)
    {
        AgentEntry agent;
        agent.window_id         = row.window_id;
        agent.process_id        = 0;
        agent.connection_fd     = -1;
        agent.assigned_figures  = row.assigned_figures;
        agent.last_heartbeat    = std::chrono::steady_clock::now();
        agent.alive             = true;
        agents_[row.window_id]  = std::move(agent);
    }

    bump_mutation();
}

ipc::WindowId SessionGraph::add_agent(ipc::ProcessId pid, int connection_fd)
{
    std::lock_guard lock(mu_);
    ipc::WindowId   wid = next_window_id_++;
    AgentEntry      entry;
    entry.window_id      = wid;
    entry.process_id     = pid;
    entry.connection_fd  = connection_fd;
    entry.last_heartbeat = std::chrono::steady_clock::now();
    entry.alive          = true;
    agents_[wid]         = std::move(entry);
    bump_mutation();
    return wid;
}

ipc::WindowId SessionGraph::claim_pending_agent(int connection_fd)
{
    std::lock_guard lock(mu_);
    for (auto& [wid, agent] : agents_)
    {
        if (agent.connection_fd == -1)
        {
            agent.connection_fd  = connection_fd;
            agent.last_heartbeat = std::chrono::steady_clock::now();
            bump_mutation();
            return wid;
        }
    }
    return ipc::INVALID_WINDOW;
}

std::vector<uint64_t> SessionGraph::remove_agent(ipc::WindowId wid)
{
    std::lock_guard lock(mu_);
    auto            it = agents_.find(wid);
    if (it == agents_.end())
        return {};

    auto figs = std::move(it->second.assigned_figures);
    agents_.erase(it);

    // Unassign figures from this window
    for (auto fig_id : figs)
    {
        auto fit = figures_.find(fig_id);
        if (fit != figures_.end())
            fit->second.assigned_window = ipc::INVALID_WINDOW;
    }

    bump_mutation();
    return figs;
}

void SessionGraph::heartbeat(ipc::WindowId wid)
{
    std::lock_guard lock(mu_);
    auto            it = agents_.find(wid);
    if (it != agents_.end())
        it->second.last_heartbeat = std::chrono::steady_clock::now();
}

std::vector<ipc::WindowId> SessionGraph::stale_agents(std::chrono::milliseconds timeout) const
{
    std::lock_guard            lock(mu_);
    auto                       now = std::chrono::steady_clock::now();
    std::vector<ipc::WindowId> result;
    for (auto& [wid, agent] : agents_)
    {
        if (agent.alive && (now - agent.last_heartbeat) > timeout)
            result.push_back(wid);
    }
    return result;
}

uint64_t SessionGraph::add_figure(const std::string& title)
{
    std::lock_guard lock(mu_);
    uint64_t        id = next_figure_id_++;
    FigureEntry     entry;
    entry.figure_id = id;
    entry.title     = title;
    figures_[id]    = std::move(entry);
    bump_mutation();
    return id;
}

void SessionGraph::register_figure(uint64_t figure_id, const std::string& title)
{
    std::lock_guard lock(mu_);
    FigureEntry     entry;
    entry.figure_id     = figure_id;
    entry.title         = title;
    figures_[figure_id] = std::move(entry);
    // Keep next_figure_id_ above any registered ID to avoid collisions
    if (figure_id >= next_figure_id_)
        next_figure_id_ = figure_id + 1;
    bump_mutation();
}

bool SessionGraph::assign_figure(uint64_t figure_id, ipc::WindowId wid)
{
    std::lock_guard lock(mu_);
    auto            fit = figures_.find(figure_id);
    if (fit == figures_.end())
        return false;

    auto ait = agents_.find(wid);
    if (ait == agents_.end())
        return false;

    // Remove from previous window if assigned
    ipc::WindowId prev = fit->second.assigned_window;
    if (prev != ipc::INVALID_WINDOW && prev != wid)
    {
        auto prev_it = agents_.find(prev);
        if (prev_it != agents_.end())
        {
            auto& pf = prev_it->second.assigned_figures;
            pf.erase(std::remove(pf.begin(), pf.end(), figure_id), pf.end());
        }
    }

    fit->second.assigned_window = wid;
    auto& af                    = ait->second.assigned_figures;
    if (std::find(af.begin(), af.end(), figure_id) == af.end())
        af.push_back(figure_id);

    bump_mutation();
    return true;
}

bool SessionGraph::unassign_figure(uint64_t figure_id, ipc::WindowId wid)
{
    std::lock_guard lock(mu_);
    auto            fit = figures_.find(figure_id);
    if (fit == figures_.end())
        return false;

    // Only unassign if currently assigned to the specified window
    if (fit->second.assigned_window != wid)
        return false;

    fit->second.assigned_window = ipc::INVALID_WINDOW;

    auto ait = agents_.find(wid);
    if (ait != agents_.end())
    {
        auto& af = ait->second.assigned_figures;
        af.erase(std::remove(af.begin(), af.end(), figure_id), af.end());
    }

    bump_mutation();
    return true;
}

void SessionGraph::remove_figure(uint64_t figure_id)
{
    std::lock_guard lock(mu_);
    auto            fit = figures_.find(figure_id);
    if (fit == figures_.end())
        return;

    // Remove from assigned window
    ipc::WindowId wid = fit->second.assigned_window;
    if (wid != ipc::INVALID_WINDOW)
    {
        auto ait = agents_.find(wid);
        if (ait != agents_.end())
        {
            auto& af = ait->second.assigned_figures;
            af.erase(std::remove(af.begin(), af.end(), figure_id), af.end());
        }
    }

    figures_.erase(fit);
    bump_mutation();
}

std::vector<uint64_t> SessionGraph::figures_for_window(ipc::WindowId wid) const
{
    std::lock_guard lock(mu_);
    auto            it = agents_.find(wid);
    if (it == agents_.end())
        return {};
    return it->second.assigned_figures;
}

size_t SessionGraph::agent_count() const
{
    std::lock_guard lock(mu_);
    return agents_.size();
}

size_t SessionGraph::figure_count() const
{
    std::lock_guard lock(mu_);
    return figures_.size();
}

bool SessionGraph::is_empty() const
{
    std::lock_guard lock(mu_);
    return agents_.empty();
}

const AgentEntry* SessionGraph::agent(ipc::WindowId wid) const
{
    std::lock_guard lock(mu_);
    auto            it = agents_.find(wid);
    if (it == agents_.end())
        return nullptr;
    return &it->second;
}

std::vector<ipc::WindowId> SessionGraph::all_window_ids() const
{
    std::lock_guard            lock(mu_);
    std::vector<ipc::WindowId> result;
    result.reserve(agents_.size());
    for (auto& [wid, _] : agents_)
        result.push_back(wid);
    return result;
}

std::vector<uint64_t> SessionGraph::unassigned_figure_ids() const
{
    std::lock_guard       lock(mu_);
    std::vector<uint64_t> result;
    for (auto& [fid, entry] : figures_)
    {
        if (entry.assigned_window == ipc::INVALID_WINDOW)
            result.push_back(fid);
    }
    return result;
}

}   // namespace spectra::daemon
