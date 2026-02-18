#include "session_graph.hpp"

#include <algorithm>

namespace spectra::daemon
{

ipc::WindowId SessionGraph::add_agent(ipc::ProcessId pid, int connection_fd)
{
    std::lock_guard lock(mu_);
    ipc::WindowId wid = next_window_id_++;
    AgentEntry entry;
    entry.window_id = wid;
    entry.process_id = pid;
    entry.connection_fd = connection_fd;
    entry.last_heartbeat = std::chrono::steady_clock::now();
    entry.alive = true;
    agents_[wid] = std::move(entry);
    return wid;
}

ipc::WindowId SessionGraph::claim_pending_agent(int connection_fd)
{
    std::lock_guard lock(mu_);
    for (auto& [wid, agent] : agents_)
    {
        if (agent.connection_fd == -1)
        {
            agent.connection_fd = connection_fd;
            agent.last_heartbeat = std::chrono::steady_clock::now();
            return wid;
        }
    }
    return ipc::INVALID_WINDOW;
}

std::vector<uint64_t> SessionGraph::remove_agent(ipc::WindowId wid)
{
    std::lock_guard lock(mu_);
    auto it = agents_.find(wid);
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

    return figs;
}

void SessionGraph::heartbeat(ipc::WindowId wid)
{
    std::lock_guard lock(mu_);
    auto it = agents_.find(wid);
    if (it != agents_.end())
        it->second.last_heartbeat = std::chrono::steady_clock::now();
}

std::vector<ipc::WindowId> SessionGraph::stale_agents(std::chrono::milliseconds timeout) const
{
    std::lock_guard lock(mu_);
    auto now = std::chrono::steady_clock::now();
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
    uint64_t id = next_figure_id_++;
    FigureEntry entry;
    entry.figure_id = id;
    entry.title = title;
    figures_[id] = std::move(entry);
    return id;
}

void SessionGraph::register_figure(uint64_t figure_id, const std::string& title)
{
    std::lock_guard lock(mu_);
    FigureEntry entry;
    entry.figure_id = figure_id;
    entry.title = title;
    figures_[figure_id] = std::move(entry);
    // Keep next_figure_id_ above any registered ID to avoid collisions
    if (figure_id >= next_figure_id_)
        next_figure_id_ = figure_id + 1;
}

bool SessionGraph::assign_figure(uint64_t figure_id, ipc::WindowId wid)
{
    std::lock_guard lock(mu_);
    auto fit = figures_.find(figure_id);
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
    auto& af = ait->second.assigned_figures;
    if (std::find(af.begin(), af.end(), figure_id) == af.end())
        af.push_back(figure_id);

    return true;
}

bool SessionGraph::unassign_figure(uint64_t figure_id, ipc::WindowId wid)
{
    std::lock_guard lock(mu_);
    auto fit = figures_.find(figure_id);
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

    return true;
}

void SessionGraph::remove_figure(uint64_t figure_id)
{
    std::lock_guard lock(mu_);
    auto fit = figures_.find(figure_id);
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
}

std::vector<uint64_t> SessionGraph::figures_for_window(ipc::WindowId wid) const
{
    std::lock_guard lock(mu_);
    auto it = agents_.find(wid);
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
    auto it = agents_.find(wid);
    if (it == agents_.end())
        return nullptr;
    return &it->second;
}

std::vector<ipc::WindowId> SessionGraph::all_window_ids() const
{
    std::lock_guard lock(mu_);
    std::vector<ipc::WindowId> result;
    result.reserve(agents_.size());
    for (auto& [wid, _] : agents_)
        result.push_back(wid);
    return result;
}

}  // namespace spectra::daemon
