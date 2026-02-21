#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef __linux__
    #include <fcntl.h>
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

namespace spectra::ipc
{

// Tracks a single shared memory blob reference.
struct BlobEntry
{
    std::string name;                                           // shm segment name
    size_t size = 0;                                            // byte size
    uint64_t figure_id = 0;                                     // owning figure
    uint32_t series_index = 0;                                  // owning series
    int pending_acks = 0;                                       // agents that haven't ACK'd yet
    std::chrono::steady_clock::time_point created_at;           // for TTL enforcement
    bool released = false;                                      // BLOB_RELEASE sent to Python
};

// Manages shared memory blob references for the backend daemon.
// The backend never writes to shm — it only tracks references and
// coordinates cleanup between Python clients and agents.
//
// Thread-safe: all public methods lock the internal mutex.
class BlobStore
{
   public:
    static constexpr auto BLOB_TTL = std::chrono::seconds(60);

    BlobStore() = default;
    ~BlobStore() { cleanup_all(); }

    // Register a new blob reference (called when Python sends TAG_BLOB_SHM).
    // Returns true if registered successfully.
    bool register_blob(const std::string& name,
                       size_t size,
                       uint64_t figure_id,
                       uint32_t series_index,
                       int agent_count)
    {
        std::lock_guard<std::mutex> lock(mu_);
        BlobEntry entry;
        entry.name = name;
        entry.size = size;
        entry.figure_id = figure_id;
        entry.series_index = series_index;
        entry.pending_acks = agent_count;
        entry.created_at = std::chrono::steady_clock::now();
        blobs_[name] = entry;
        return true;
    }

    // Called when an agent ACKs that it has read the blob.
    // Returns true if all agents have ACK'd (blob can be released).
    bool ack_blob(const std::string& name)
    {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = blobs_.find(name);
        if (it == blobs_.end())
            return false;
        it->second.pending_acks--;
        return it->second.pending_acks <= 0;
    }

    // Mark a blob as released (BLOB_RELEASE sent to Python).
    void mark_released(const std::string& name)
    {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = blobs_.find(name);
        if (it != blobs_.end())
        {
            it->second.released = true;
            blobs_.erase(it);
        }
    }

    // Find and unlink expired blobs. Returns names of expired blobs.
    std::vector<std::string> cleanup_expired()
    {
        std::lock_guard<std::mutex> lock(mu_);
        auto now = std::chrono::steady_clock::now();
        std::vector<std::string> expired;

        for (auto it = blobs_.begin(); it != blobs_.end();)
        {
            if ((now - it->second.created_at) > BLOB_TTL)
            {
                expired.push_back(it->first);
                unlink_shm(it->first);
                it = blobs_.erase(it);
            }
            else
            {
                ++it;
            }
        }
        return expired;
    }

    // Cleanup all blobs for a session teardown.
    void cleanup_all()
    {
        std::lock_guard<std::mutex> lock(mu_);
        for (auto& [name, entry] : blobs_)
            unlink_shm(name);
        blobs_.clear();
    }

    // Get names of blobs ready for release (all agents ACK'd).
    std::vector<std::string> releasable_blobs() const
    {
        std::lock_guard<std::mutex> lock(mu_);
        std::vector<std::string> result;
        for (auto& [name, entry] : blobs_)
        {
            if (entry.pending_acks <= 0 && !entry.released)
                result.push_back(name);
        }
        return result;
    }

    size_t active_count() const
    {
        std::lock_guard<std::mutex> lock(mu_);
        return blobs_.size();
    }

   private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, BlobEntry> blobs_;

    static void unlink_shm([[maybe_unused]] const std::string& name)
    {
#ifdef __linux__
        ::shm_unlink(name.c_str());
#endif
    }
};

}  // namespace spectra::ipc
