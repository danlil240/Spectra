#include "figure_model.hpp"

#include <algorithm>

namespace spectra::daemon
{

// --- Figure lifecycle ---

uint64_t FigureModel::create_figure(const std::string& title, uint32_t width, uint32_t height)
{
    std::lock_guard lock(mu_);
    uint64_t id = next_figure_id_++;
    FigureData fd;
    fd.id = id;
    fd.title = title;
    fd.width = width;
    fd.height = height;
    figures_[id] = std::move(fd);
    figure_order_.push_back(id);
    bump_revision();
    return id;
}

bool FigureModel::remove_figure(uint64_t figure_id)
{
    std::lock_guard lock(mu_);
    auto it = figures_.find(figure_id);
    if (it == figures_.end())
        return false;
    figures_.erase(it);
    figure_order_.erase(std::remove(figure_order_.begin(), figure_order_.end(), figure_id),
                        figure_order_.end());
    bump_revision();
    return true;
}

// --- Axes management ---

void FigureModel::set_grid(uint64_t figure_id, int32_t rows, int32_t cols)
{
    std::lock_guard lock(mu_);
    auto it = figures_.find(figure_id);
    if (it == figures_.end())
        return;
    // Only grow — never shrink the grid
    if (rows > it->second.grid_rows)
        it->second.grid_rows = rows;
    if (cols > it->second.grid_cols)
        it->second.grid_cols = cols;
}

uint32_t FigureModel::add_axes(
    uint64_t figure_id, float x_min, float x_max, float y_min, float y_max, bool is_3d)
{
    std::lock_guard lock(mu_);
    auto it = figures_.find(figure_id);
    if (it == figures_.end())
        return 0;
    ipc::SnapshotAxisState ax;
    ax.x_min = x_min;
    ax.x_max = x_max;
    ax.y_min = y_min;
    ax.y_max = y_max;
    ax.is_3d = is_3d;
    it->second.axes.push_back(std::move(ax));
    bump_revision();
    return static_cast<uint32_t>(it->second.axes.size() - 1);
}

ipc::DiffOp FigureModel::set_axis_limits(
    uint64_t figure_id, uint32_t axes_index, float x_min, float x_max, float y_min, float y_max)
{
    std::lock_guard lock(mu_);
    auto it = figures_.find(figure_id);
    if (it != figures_.end() && axes_index < it->second.axes.size())
    {
        auto& ax = it->second.axes[axes_index];
        ax.x_min = x_min;
        ax.x_max = x_max;
        ax.y_min = y_min;
        ax.y_max = y_max;
    }
    bump_revision();

    ipc::DiffOp op;
    op.type = ipc::DiffOp::Type::SET_AXIS_LIMITS;
    op.figure_id = figure_id;
    op.axes_index = axes_index;
    op.f1 = x_min;
    op.f2 = x_max;
    op.f3 = y_min;
    op.f4 = y_max;
    return op;
}

ipc::DiffOp FigureModel::set_axis_zlimits(
    uint64_t figure_id, uint32_t axes_index, float z_min, float z_max)
{
    std::lock_guard lock(mu_);
    auto it = figures_.find(figure_id);
    if (it != figures_.end() && axes_index < it->second.axes.size())
    {
        auto& ax = it->second.axes[axes_index];
        ax.z_min = z_min;
        ax.z_max = z_max;
    }
    bump_revision();

    ipc::DiffOp op;
    op.type = ipc::DiffOp::Type::SET_AXIS_ZLIMITS;
    op.figure_id = figure_id;
    op.axes_index = axes_index;
    op.f1 = z_min;
    op.f2 = z_max;
    return op;
}

ipc::DiffOp FigureModel::set_grid_visible(uint64_t figure_id, uint32_t axes_index, bool visible)
{
    std::lock_guard lock(mu_);
    auto it = figures_.find(figure_id);
    if (it != figures_.end() && axes_index < it->second.axes.size())
        it->second.axes[axes_index].grid_visible = visible;
    bump_revision();

    ipc::DiffOp op;
    op.type = ipc::DiffOp::Type::SET_GRID_VISIBLE;
    op.figure_id = figure_id;
    op.axes_index = axes_index;
    op.bool_val = visible;
    return op;
}

ipc::DiffOp FigureModel::set_axis_xlabel(uint64_t figure_id, uint32_t axes_index, const std::string& label)
{
    std::lock_guard lock(mu_);
    auto it = figures_.find(figure_id);
    if (it != figures_.end() && axes_index < it->second.axes.size())
        it->second.axes[axes_index].x_label = label;
    bump_revision();

    ipc::DiffOp op;
    op.type = ipc::DiffOp::Type::SET_AXIS_XLABEL;
    op.figure_id = figure_id;
    op.axes_index = axes_index;
    op.str_val = label;
    return op;
}

ipc::DiffOp FigureModel::set_axis_ylabel(uint64_t figure_id, uint32_t axes_index, const std::string& label)
{
    std::lock_guard lock(mu_);
    auto it = figures_.find(figure_id);
    if (it != figures_.end() && axes_index < it->second.axes.size())
        it->second.axes[axes_index].y_label = label;
    bump_revision();

    ipc::DiffOp op;
    op.type = ipc::DiffOp::Type::SET_AXIS_YLABEL;
    op.figure_id = figure_id;
    op.axes_index = axes_index;
    op.str_val = label;
    return op;
}

ipc::DiffOp FigureModel::set_axis_title(uint64_t figure_id, uint32_t axes_index, const std::string& title)
{
    std::lock_guard lock(mu_);
    auto it = figures_.find(figure_id);
    if (it != figures_.end() && axes_index < it->second.axes.size())
        it->second.axes[axes_index].title = title;
    bump_revision();

    ipc::DiffOp op;
    op.type = ipc::DiffOp::Type::SET_AXIS_TITLE;
    op.figure_id = figure_id;
    op.axes_index = axes_index;
    op.str_val = title;
    return op;
}

// --- Series management ---

ipc::DiffOp FigureModel::set_series_label(uint64_t figure_id, uint32_t series_index, const std::string& label)
{
    std::lock_guard lock(mu_);
    auto it = figures_.find(figure_id);
    if (it != figures_.end() && series_index < it->second.series.size())
        it->second.series[series_index].name = label;
    bump_revision();

    ipc::DiffOp op;
    op.type = ipc::DiffOp::Type::SET_SERIES_LABEL;
    op.figure_id = figure_id;
    op.series_index = series_index;
    op.str_val = label;
    return op;
}

uint32_t FigureModel::add_series(uint64_t figure_id,
                                 const std::string& name,
                                 const std::string& type)
{
    std::lock_guard lock(mu_);
    auto it = figures_.find(figure_id);
    if (it == figures_.end())
        return 0;
    ipc::SnapshotSeriesState s;
    s.name = name;
    s.type = type;
    it->second.series.push_back(std::move(s));
    bump_revision();
    return static_cast<uint32_t>(it->second.series.size() - 1);
}

ipc::DiffOp FigureModel::add_series_with_diff(uint64_t figure_id,
                                              const std::string& name,
                                              const std::string& type,
                                              uint32_t axes_index,
                                              uint32_t& out_index)
{
    std::lock_guard lock(mu_);
    auto it = figures_.find(figure_id);
    if (it == figures_.end())
    {
        out_index = 0;
        return {};
    }
    ipc::SnapshotSeriesState s;
    s.name = name;
    s.type = type;
    it->second.series.push_back(std::move(s));
    out_index = static_cast<uint32_t>(it->second.series.size() - 1);
    bump_revision();

    ipc::DiffOp op;
    op.type = ipc::DiffOp::Type::ADD_SERIES;
    op.figure_id = figure_id;
    op.axes_index = axes_index;
    op.series_index = out_index;
    op.str_val = type;
    return op;
}

ipc::DiffOp FigureModel::set_series_color(
    uint64_t figure_id, uint32_t series_index, float r, float g, float b, float a)
{
    std::lock_guard lock(mu_);
    auto it = figures_.find(figure_id);
    if (it != figures_.end() && series_index < it->second.series.size())
    {
        auto& s = it->second.series[series_index];
        s.color_r = r;
        s.color_g = g;
        s.color_b = b;
        s.color_a = a;
    }
    bump_revision();

    ipc::DiffOp op;
    op.type = ipc::DiffOp::Type::SET_SERIES_COLOR;
    op.figure_id = figure_id;
    op.series_index = series_index;
    op.f1 = r;
    op.f2 = g;
    op.f3 = b;
    op.f4 = a;
    return op;
}

ipc::DiffOp FigureModel::set_series_visible(uint64_t figure_id, uint32_t series_index, bool visible)
{
    std::lock_guard lock(mu_);
    auto it = figures_.find(figure_id);
    if (it != figures_.end() && series_index < it->second.series.size())
        it->second.series[series_index].visible = visible;
    bump_revision();

    ipc::DiffOp op;
    op.type = ipc::DiffOp::Type::SET_SERIES_VISIBLE;
    op.figure_id = figure_id;
    op.series_index = series_index;
    op.bool_val = visible;
    return op;
}

ipc::DiffOp FigureModel::set_line_width(uint64_t figure_id, uint32_t series_index, float width)
{
    std::lock_guard lock(mu_);
    auto it = figures_.find(figure_id);
    if (it != figures_.end() && series_index < it->second.series.size())
        it->second.series[series_index].line_width = width;
    bump_revision();

    ipc::DiffOp op;
    op.type = ipc::DiffOp::Type::SET_LINE_WIDTH;
    op.figure_id = figure_id;
    op.series_index = series_index;
    op.f1 = width;
    return op;
}

ipc::DiffOp FigureModel::set_marker_size(uint64_t figure_id, uint32_t series_index, float size)
{
    std::lock_guard lock(mu_);
    auto it = figures_.find(figure_id);
    if (it != figures_.end() && series_index < it->second.series.size())
        it->second.series[series_index].marker_size = size;
    bump_revision();

    ipc::DiffOp op;
    op.type = ipc::DiffOp::Type::SET_MARKER_SIZE;
    op.figure_id = figure_id;
    op.series_index = series_index;
    op.f1 = size;
    return op;
}

ipc::DiffOp FigureModel::set_opacity(uint64_t figure_id, uint32_t series_index, float opacity)
{
    std::lock_guard lock(mu_);
    auto it = figures_.find(figure_id);
    if (it != figures_.end() && series_index < it->second.series.size())
        it->second.series[series_index].opacity = opacity;
    bump_revision();

    ipc::DiffOp op;
    op.type = ipc::DiffOp::Type::SET_OPACITY;
    op.figure_id = figure_id;
    op.series_index = series_index;
    op.f1 = opacity;
    return op;
}

ipc::DiffOp FigureModel::remove_series(uint64_t figure_id, uint32_t series_index)
{
    std::lock_guard lock(mu_);
    auto it = figures_.find(figure_id);
    if (it != figures_.end() && series_index < it->second.series.size())
    {
        it->second.series.erase(it->second.series.begin() + series_index);
    }
    bump_revision();

    ipc::DiffOp op;
    op.type = ipc::DiffOp::Type::REMOVE_SERIES;
    op.figure_id = figure_id;
    op.series_index = series_index;
    return op;
}

ipc::DiffOp FigureModel::set_series_data(uint64_t figure_id,
                                         uint32_t series_index,
                                         const std::vector<float>& data)
{
    std::lock_guard lock(mu_);
    auto it = figures_.find(figure_id);
    if (it != figures_.end() && series_index < it->second.series.size())
    {
        it->second.series[series_index].data = data;
        it->second.series[series_index].point_count = static_cast<uint32_t>(data.size() / 2);
    }
    bump_revision();

    ipc::DiffOp op;
    op.type = ipc::DiffOp::Type::SET_SERIES_DATA;
    op.figure_id = figure_id;
    op.series_index = series_index;
    op.data = data;
    return op;
}

ipc::DiffOp FigureModel::append_series_data(uint64_t figure_id,
                                            uint32_t series_index,
                                            const std::vector<float>& data)
{
    std::lock_guard lock(mu_);
    auto it = figures_.find(figure_id);
    if (it != figures_.end() && series_index < it->second.series.size())
    {
        auto& sd = it->second.series[series_index].data;
        sd.insert(sd.end(), data.begin(), data.end());
        it->second.series[series_index].point_count = static_cast<uint32_t>(sd.size() / 2);
    }
    bump_revision();

    // For the diff, we send the full updated data so agents get the complete state.
    // This is simpler than a partial append diff op and avoids ordering issues.
    ipc::DiffOp op;
    op.type = ipc::DiffOp::Type::SET_SERIES_DATA;
    op.figure_id = figure_id;
    op.series_index = series_index;
    if (it != figures_.end() && series_index < it->second.series.size())
        op.data = it->second.series[series_index].data;
    return op;
}

ipc::DiffOp FigureModel::set_figure_title(uint64_t figure_id, const std::string& title)
{
    std::lock_guard lock(mu_);
    auto it = figures_.find(figure_id);
    if (it != figures_.end())
        it->second.title = title;
    bump_revision();

    ipc::DiffOp op;
    op.type = ipc::DiffOp::Type::SET_FIGURE_TITLE;
    op.figure_id = figure_id;
    op.str_val = title;
    return op;
}

// --- Load snapshot (app → backend push) ---

std::vector<uint64_t> FigureModel::load_snapshot(const ipc::StateSnapshotPayload& snap)
{
    std::lock_guard lock(mu_);
    figures_.clear();
    figure_order_.clear();
    knobs_ = snap.knobs;  // store knob definitions

    std::vector<uint64_t> ids;
    for (const auto& fig : snap.figures)
    {
        uint64_t id = fig.figure_id;
        if (id == 0)
            id = next_figure_id_++;
        else if (id >= next_figure_id_)
            next_figure_id_ = id + 1;

        FigureData fd;
        fd.id = id;
        fd.title = fig.title;
        fd.width = fig.width;
        fd.height = fig.height;
        fd.grid_rows = fig.grid_rows;
        fd.grid_cols = fig.grid_cols;
        fd.axes = fig.axes;
        fd.series = fig.series;
        figures_[id] = std::move(fd);
        figure_order_.push_back(id);
        ids.push_back(id);
    }
    bump_revision();
    return ids;
}

// --- Snapshot ---

ipc::StateSnapshotPayload FigureModel::snapshot() const
{
    std::lock_guard lock(mu_);
    ipc::StateSnapshotPayload snap;
    snap.revision = revision_;
    snap.session_id = 1;  // single session
    for (auto id : figure_order_)
    {
        auto it = figures_.find(id);
        if (it == figures_.end())
            continue;
        const auto& fd = it->second;
        ipc::SnapshotFigureState fig;
        fig.figure_id = fd.id;
        fig.title = fd.title;
        fig.width = fd.width;
        fig.height = fd.height;
        fig.grid_rows = fd.grid_rows;
        fig.grid_cols = fd.grid_cols;
        fig.axes = fd.axes;
        fig.series = fd.series;
        snap.figures.push_back(std::move(fig));
    }
    snap.knobs = knobs_;
    return snap;
}

ipc::StateSnapshotPayload FigureModel::snapshot(const std::vector<uint64_t>& figure_ids) const
{
    std::lock_guard lock(mu_);
    ipc::StateSnapshotPayload snap;
    snap.revision = revision_;
    snap.session_id = 1;
    for (auto id : figure_ids)
    {
        auto it = figures_.find(id);
        if (it == figures_.end())
            continue;
        const auto& fd = it->second;
        ipc::SnapshotFigureState fig;
        fig.figure_id = fd.id;
        fig.title = fd.title;
        fig.width = fd.width;
        fig.height = fd.height;
        fig.grid_rows = fd.grid_rows;
        fig.grid_cols = fd.grid_cols;
        fig.axes = fd.axes;
        fig.series = fd.series;
        snap.figures.push_back(std::move(fig));
    }
    snap.knobs = knobs_;
    return snap;
}

// --- Apply diff ---

bool FigureModel::apply_diff_op(const ipc::DiffOp& op)
{
    std::lock_guard lock(mu_);
    auto it = figures_.find(op.figure_id);

    switch (op.type)
    {
        case ipc::DiffOp::Type::SET_AXIS_LIMITS:
            if (it == figures_.end() || op.axes_index >= it->second.axes.size())
                return false;
            it->second.axes[op.axes_index].x_min = op.f1;
            it->second.axes[op.axes_index].x_max = op.f2;
            it->second.axes[op.axes_index].y_min = op.f3;
            it->second.axes[op.axes_index].y_max = op.f4;
            break;

        case ipc::DiffOp::Type::SET_SERIES_COLOR:
            if (it == figures_.end() || op.series_index >= it->second.series.size())
                return false;
            it->second.series[op.series_index].color_r = op.f1;
            it->second.series[op.series_index].color_g = op.f2;
            it->second.series[op.series_index].color_b = op.f3;
            it->second.series[op.series_index].color_a = op.f4;
            break;

        case ipc::DiffOp::Type::SET_SERIES_VISIBLE:
            if (it == figures_.end() || op.series_index >= it->second.series.size())
                return false;
            it->second.series[op.series_index].visible = op.bool_val;
            break;

        case ipc::DiffOp::Type::SET_FIGURE_TITLE:
            if (it == figures_.end())
                return false;
            it->second.title = op.str_val;
            break;

        case ipc::DiffOp::Type::SET_GRID_VISIBLE:
            if (it == figures_.end() || op.axes_index >= it->second.axes.size())
                return false;
            it->second.axes[op.axes_index].grid_visible = op.bool_val;
            break;

        case ipc::DiffOp::Type::SET_LINE_WIDTH:
            if (it == figures_.end() || op.series_index >= it->second.series.size())
                return false;
            it->second.series[op.series_index].line_width = op.f1;
            break;

        case ipc::DiffOp::Type::SET_MARKER_SIZE:
            if (it == figures_.end() || op.series_index >= it->second.series.size())
                return false;
            it->second.series[op.series_index].marker_size = op.f1;
            break;

        case ipc::DiffOp::Type::SET_OPACITY:
            if (it == figures_.end() || op.series_index >= it->second.series.size())
                return false;
            it->second.series[op.series_index].opacity = op.f1;
            break;

        case ipc::DiffOp::Type::SET_SERIES_DATA:
            if (it == figures_.end() || op.series_index >= it->second.series.size())
                return false;
            it->second.series[op.series_index].data = op.data;
            it->second.series[op.series_index].point_count =
                static_cast<uint32_t>(op.data.size() / 2);
            break;

        case ipc::DiffOp::Type::SET_AXIS_XLABEL:
            if (it == figures_.end() || op.axes_index >= it->second.axes.size())
                return false;
            it->second.axes[op.axes_index].x_label = op.str_val;
            break;

        case ipc::DiffOp::Type::SET_AXIS_YLABEL:
            if (it == figures_.end() || op.axes_index >= it->second.axes.size())
                return false;
            it->second.axes[op.axes_index].y_label = op.str_val;
            break;

        case ipc::DiffOp::Type::SET_AXIS_TITLE:
            if (it == figures_.end() || op.axes_index >= it->second.axes.size())
                return false;
            it->second.axes[op.axes_index].title = op.str_val;
            break;

        case ipc::DiffOp::Type::SET_SERIES_LABEL:
            if (it == figures_.end() || op.series_index >= it->second.series.size())
                return false;
            it->second.series[op.series_index].name = op.str_val;
            break;

        case ipc::DiffOp::Type::ADD_FIGURE:
        case ipc::DiffOp::Type::REMOVE_FIGURE:
            // These are handled via create_figure/remove_figure directly
            return false;

        default:
            return false;
    }

    bump_revision();
    return true;
}

// --- Queries ---

ipc::Revision FigureModel::revision() const
{
    std::lock_guard lock(mu_);
    return revision_;
}

size_t FigureModel::figure_count() const
{
    std::lock_guard lock(mu_);
    return figures_.size();
}

std::vector<uint64_t> FigureModel::all_figure_ids() const
{
    std::lock_guard lock(mu_);
    return figure_order_;
}

bool FigureModel::has_figure(uint64_t figure_id) const
{
    std::lock_guard lock(mu_);
    return figures_.count(figure_id) > 0;
}

bool FigureModel::get_axis_limits(uint64_t figure_id, uint32_t axes_index,
                                  float& x_min, float& x_max, float& y_min, float& y_max) const
{
    std::lock_guard lock(mu_);
    auto it = figures_.find(figure_id);
    if (it == figures_.end() || axes_index >= it->second.axes.size())
        return false;
    const auto& ax = it->second.axes[axes_index];
    x_min = ax.x_min;
    x_max = ax.x_max;
    y_min = ax.y_min;
    y_max = ax.y_max;
    return true;
}

}  // namespace spectra::daemon
