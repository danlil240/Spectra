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
                           uint32_t           width  = DEFAULT_WINDOW_WIDTH,
                           uint32_t           height = DEFAULT_WINDOW_HEIGHT);

    // Remove a figure by ID. Returns true if found and removed.
    bool remove_figure(uint64_t figure_id);

    // --- Axes management ---

    // Update the subplot grid dimensions for a figure.
    void set_grid(uint64_t figure_id, int32_t rows, int32_t cols);

    // Read the current subplot grid dimensions. Returns false if the figure is unknown.
    bool get_grid(uint64_t figure_id, int32_t& rows, int32_t& cols) const;

    // Add an axes to a figure. Returns the axes index.
    uint32_t add_axes(uint64_t figure_id,
                      double   x_min = 0.0,
                      double   x_max = 1.0,
                      double   y_min = 0.0,
                      double   y_max = 1.0,
                      bool     is_3d = false);

    // Set axis limits. Returns a DiffOp for broadcasting.
    ipc::DiffOp set_axis_limits(uint64_t figure_id,
                                uint32_t axes_index,
                                double   x_min,
                                double   x_max,
                                double   y_min,
                                double   y_max);

    // Set 3D z-axis limits. Returns a DiffOp for broadcasting.
    ipc::DiffOp set_axis_zlimits(uint64_t figure_id,
                                 uint32_t axes_index,
                                 double   z_min,
                                 double   z_max);

    // Set grid visibility. Returns a DiffOp.
    ipc::DiffOp set_grid_visible(uint64_t figure_id, uint32_t axes_index, bool visible);

    // Set axis xlabel. Returns a DiffOp.
    ipc::DiffOp set_axis_xlabel(uint64_t figure_id, uint32_t axes_index, const std::string& label);

    // Set axis ylabel. Returns a DiffOp.
    ipc::DiffOp set_axis_ylabel(uint64_t figure_id, uint32_t axes_index, const std::string& label);

    // Set axis title. Returns a DiffOp.
    ipc::DiffOp set_axis_title(uint64_t figure_id, uint32_t axes_index, const std::string& title);

    // --- Text annotations (2D axes) ---
    // Each returns false (no change, no revision bump) when the figure, axes or
    // annotation index does not exist; on success *op_out holds the diff.

    // Append an annotation to an axes.
    bool add_annotation(uint64_t                            figure_id,
                        uint32_t                            axes_index,
                        const ipc::SnapshotAnnotationState& annotation,
                        ipc::DiffOp*                        op_out);

    // Replace annotation @p index of an axes.
    bool set_annotation(uint64_t                            figure_id,
                        uint32_t                            axes_index,
                        uint32_t                            index,
                        const ipc::SnapshotAnnotationState& annotation,
                        ipc::DiffOp*                        op_out);

    // Remove every annotation of an axes.
    bool clear_annotations(uint64_t figure_id, uint32_t axes_index, ipc::DiffOp* op_out);

    // Number of annotations on an axes (0 when unknown).
    size_t annotation_count(uint64_t figure_id, uint32_t axes_index) const;

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

    // Colour a series with MATLAB format letter @p letter (r g b c m y k w).
    // The window resolves the letter's shade for its theme; the op carries the
    // dark-background shade in f1..f4 and the letter in str_val.  A non-letter
    // leaves the series unchanged (the op then carries its current colour).
    ipc::DiffOp set_series_color_letter(uint64_t figure_id, uint32_t series_index, char letter);

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
    // Every later series index shifts down by one. When the figure or index
    // does not exist nothing changes (no revision bump) and *removed_out is
    // set to false; callers must not broadcast the returned op in that case.
    ipc::DiffOp remove_series(uint64_t figure_id,
                              uint32_t series_index,
                              bool*    removed_out = nullptr);

    // Set series data (raw floats). Returns a DiffOp.
    ipc::DiffOp set_series_data(uint64_t                  figure_id,
                                uint32_t                  series_index,
                                const std::vector<float>& data);

    // Append data to existing series (streaming). Returns a DiffOp.
    // When max_points > 0 the series is trimmed to its newest max_points
    // points (rolling window), which also bounds the size of the returned diff.
    ipc::DiffOp append_series_data(uint64_t                  figure_id,
                                   uint32_t                  series_index,
                                   const std::vector<float>& data,
                                   size_t                    max_points = 0);

    // Set figure title. Returns a DiffOp.
    ipc::DiffOp set_figure_title(uint64_t figure_id, const std::string& title);

    // Set live streaming FPS for a figure. 0 disables live streaming.
    void set_live_fps(uint64_t figure_id, float fps);

    // Interactive parameter knobs (shown in the PARAMETERS panel).
    bool add_knob(const ipc::SnapshotKnobState& knob);
    bool set_knob_value(const std::string& name, float value);

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
                         double&  x_min,
                         double&  x_max,
                         double&  y_min,
                         double&  y_max) const;

   private:
    mutable std::mutex mu_;
    ipc::Revision      revision_       = 0;
    uint64_t           next_figure_id_ = 1;

    // Internal figure state (mirrors SnapshotFigureState but mutable)
    struct FigureData
    {
        uint64_t                              id = 0;
        std::string                           title;
        uint32_t                              width     = DEFAULT_WINDOW_WIDTH;
        uint32_t                              height    = DEFAULT_WINDOW_HEIGHT;
        int32_t                               grid_rows = 1;
        int32_t                               grid_cols = 1;
        float                                 live_fps  = 0.0f;
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
