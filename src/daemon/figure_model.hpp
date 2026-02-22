#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "../ipc/message.hpp"

namespace spectra::daemon
{

// Authoritative figure model owned by the backend daemon.
// All mutations go through this class; it tracks revisions and can
// produce STATE_SNAPSHOT and STATE_DIFF payloads.
// Thread-safe — all public methods lock the internal mutex.
class FigureModel
{
   public:
    FigureModel() = default;

    // --- Figure lifecycle ---

    // Create a new figure with default state. Returns the figure ID.
    uint64_t create_figure(const std::string& title  = "Figure",
                           uint32_t           width  = 1280,
                           uint32_t           height = 720);

    // Remove a figure by ID. Returns true if found and removed.
    bool remove_figure(uint64_t figure_id);

    // --- Axes management ---

    // Update the subplot grid dimensions for a figure.
    void set_grid(uint64_t figure_id, int32_t rows, int32_t cols);

    // Add an axes to a figure. Returns the axes index.
    uint32_t add_axes(uint64_t figure_id,
                      float    x_min = 0.0f,
                      float    x_max = 1.0f,
                      float    y_min = 0.0f,
                      float    y_max = 1.0f,
                      bool     is_3d = false);

    // Set axis limits. Returns a DiffOp for broadcasting.
    ipc::DiffOp set_axis_limits(uint64_t figure_id,
                                uint32_t axes_index,
                                float    x_min,
                                float    x_max,
                                float    y_min,
                                float    y_max);

    // Set 3D z-axis limits. Returns a DiffOp for broadcasting.
    ipc::DiffOp set_axis_zlimits(uint64_t figure_id, uint32_t axes_index, float z_min, float z_max);

    // Set grid visibility. Returns a DiffOp.
    ipc::DiffOp set_grid_visible(uint64_t figure_id, uint32_t axes_index, bool visible);

    // Set axis xlabel. Returns a DiffOp.
    ipc::DiffOp set_axis_xlabel(uint64_t figure_id, uint32_t axes_index, const std::string& label);

    // Set axis ylabel. Returns a DiffOp.
    ipc::DiffOp set_axis_ylabel(uint64_t figure_id, uint32_t axes_index, const std::string& label);

    // Set axis title. Returns a DiffOp.
    ipc::DiffOp set_axis_title(uint64_t figure_id, uint32_t axes_index, const std::string& title);

    // --- Series management ---

    // Add a series to a figure's axes. Returns the series index.
    uint32_t add_series(uint64_t           figure_id,
                        const std::string& name = "",
                        const std::string& type = "line");

    // Add a series and return a DiffOp for broadcasting to agents.
    // out_index receives the new series index.
    ipc::DiffOp add_series_with_diff(uint64_t           figure_id,
                                     const std::string& name,
                                     const std::string& type,
                                     uint32_t           axes_index,
                                     uint32_t&          out_index);

    // Set series color. Returns a DiffOp.
    ipc::DiffOp set_series_color(uint64_t figure_id,
                                 uint32_t series_index,
                                 float    r,
                                 float    g,
                                 float    b,
                                 float    a);

    // Set series visibility. Returns a DiffOp.
    ipc::DiffOp set_series_visible(uint64_t figure_id, uint32_t series_index, bool visible);

    // Set series line width. Returns a DiffOp.
    ipc::DiffOp set_line_width(uint64_t figure_id, uint32_t series_index, float width);

    // Set series marker size. Returns a DiffOp.
    ipc::DiffOp set_marker_size(uint64_t figure_id, uint32_t series_index, float size);

    // Set series opacity. Returns a DiffOp.
    ipc::DiffOp set_opacity(uint64_t figure_id, uint32_t series_index, float opacity);

    // Set series label/name. Returns a DiffOp.
    ipc::DiffOp set_series_label(uint64_t           figure_id,
                                 uint32_t           series_index,
                                 const std::string& label);

    // Remove a series from a figure. Returns a DiffOp for broadcasting.
    ipc::DiffOp remove_series(uint64_t figure_id, uint32_t series_index);

    // Set series data (raw floats). Returns a DiffOp.
    ipc::DiffOp set_series_data(uint64_t                  figure_id,
                                uint32_t                  series_index,
                                const std::vector<float>& data);

    // Append data to existing series (streaming). Returns a DiffOp.
    ipc::DiffOp append_series_data(uint64_t                  figure_id,
                                   uint32_t                  series_index,
                                   const std::vector<float>& data);

    // Set figure title. Returns a DiffOp.
    ipc::DiffOp set_figure_title(uint64_t figure_id, const std::string& title);

    // --- Snapshot / Diff ---

    // Produce a full STATE_SNAPSHOT of all figures (or a subset by ID).
    ipc::StateSnapshotPayload snapshot() const;
    ipc::StateSnapshotPayload snapshot(const std::vector<uint64_t>& figure_ids) const;

    // Replace all figures from an incoming StateSnapshotPayload (app → backend push).
    // Clears existing figures and loads from the snapshot. Returns new figure IDs.
    std::vector<uint64_t> load_snapshot(const ipc::StateSnapshotPayload& snap);

    // Apply a DiffOp to the model (used when receiving EVT_INPUT mutations).
    // Returns true if the op was applied successfully.
    bool apply_diff_op(const ipc::DiffOp& op);

    // Current revision number.
    ipc::Revision revision() const;

    // --- Queries ---

    size_t                figure_count() const;
    std::vector<uint64_t> all_figure_ids() const;
    bool                  has_figure(uint64_t figure_id) const;

    // Get current axis limits for a figure's axes. Returns false if not found.
    bool get_axis_limits(uint64_t figure_id,
                         uint32_t axes_index,
                         float&   x_min,
                         float&   x_max,
                         float&   y_min,
                         float&   y_max) const;

   private:
    mutable std::mutex mu_;
    ipc::Revision      revision_       = 0;
    uint64_t           next_figure_id_ = 1;

    // Internal figure state (mirrors SnapshotFigureState but mutable)
    struct FigureData
    {
        uint64_t                              id = 0;
        std::string                           title;
        uint32_t                              width     = 1280;
        uint32_t                              height    = 720;
        int32_t                               grid_rows = 1;
        int32_t                               grid_cols = 1;
        std::vector<ipc::SnapshotAxisState>   axes;
        std::vector<ipc::SnapshotSeriesState> series;
    };

    std::unordered_map<uint64_t, FigureData> figures_;
    std::vector<uint64_t>                    figure_order_;   // insertion order
    std::vector<ipc::SnapshotKnobState>      knobs_;          // interactive parameter knobs

    // Bump revision (caller must hold lock).
    void bump_revision() { ++revision_; }
};

}   // namespace spectra::daemon
