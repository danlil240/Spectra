#pragma once

#include <cstdint>
#include <string>

namespace spectra::daemon
{

class FigureModel;
class SessionGraph;

// Optional Postgres persistence for daemon session state.
// Enabled at compile time with SPECTRA_USE_POSTGRES and at runtime via DATABASE_URL.
class SessionStore
{
   public:
    SessionStore();
    ~SessionStore();

    SessionStore(const SessionStore&)            = delete;
    SessionStore& operator=(const SessionStore&) = delete;

    bool enabled() const { return enabled_; }

    // Connect using DATABASE_URL (if compiled in). Safe to call repeatedly.
    bool init();

    // Load the most recently updated session into graph + figure model.
    bool try_load(SessionGraph& graph, FigureModel& model);

    // Upsert current session state.
    bool save(const SessionGraph& graph, const FigureModel& model);

   private:
    bool enabled_ = false;
#ifdef SPECTRA_USE_POSTGRES
    struct PgConn;
    PgConn* conn_ = nullptr;
#endif
};

}   // namespace spectra::daemon
