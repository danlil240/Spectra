// data_sidebar_model.cpp — Data sidebar state, filtering and actions.

#include "ui/data_sidebar/data_sidebar_model.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace spectra::ui::data_sidebar
{

namespace
{

char lower(char c)
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool contains_ci(std::string_view hay, std::string_view needle)
{
    if (needle.empty())
        return true;
    if (needle.size() > hay.size())
        return false;
    const auto it = std::search(hay.begin(),
                                hay.end(),
                                needle.begin(),
                                needle.end(),
                                [](char a, char b) { return lower(a) == lower(b); });
    return it != hay.end();
}

bool is_space(char c)
{
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

bool is_container(RowKind k)
{
    return k == RowKind::Provider || k == RowKind::Source || k == RowKind::Group;
}

}   // namespace

bool field_matches_query(std::string_view source_name,
                         const DataField& field,
                         std::string_view query)
{
    std::string hay;
    hay.reserve(source_name.size() + field.group.size() + field.name.size() + 2);
    hay.append(source_name).append("/").append(field.group).append("/").append(field.name);

    size_t i = 0;
    while (i < query.size())
    {
        while (i < query.size() && is_space(query[i]))
            ++i;
        size_t j = i;
        while (j < query.size() && !is_space(query[j]))
            ++j;
        if (j > i && !contains_ci(hay, query.substr(i, j - i)))
            return false;
        i = j;
    }
    return true;
}

// ─── Providers ───────────────────────────────────────────────────────────────

DataProvider* DataSidebarModel::add_provider(std::unique_ptr<DataProvider> provider)
{
    if (!provider)
        return nullptr;
    providers_.push_back(std::move(provider));
    sources_.emplace_back();
    return providers_.back().get();
}

DataProvider* DataSidebarModel::provider(size_t index) const
{
    return index < providers_.size() ? providers_[index].get() : nullptr;
}

DataProvider* DataSidebarModel::find_provider(std::string_view id) const
{
    for (const auto& p : providers_)
    {
        if (p->id() == id)
            return p.get();
    }
    return nullptr;
}

const std::vector<DataSource>& DataSidebarModel::sources(size_t provider) const
{
    static const std::vector<DataSource> kEmpty;
    return provider < sources_.size() ? sources_[provider] : kEmpty;
}

void DataSidebarModel::refresh()
{
    for (size_t i = 0; i < providers_.size(); ++i)
    {
        sources_[i].clear();
        providers_[i]->list_sources(sources_[i]);
    }
    rebuild_rows();
}

// ─── Search ──────────────────────────────────────────────────────────────────

bool DataSidebarModel::filtering() const
{
    return std::any_of(query_.begin(), query_.end(), [](char c) { return !is_space(c); });
}

void DataSidebarModel::set_query(std::string query)
{
    if (query == query_)
        return;
    query_ = std::move(query);
    rebuild_rows();
    // Typing then Enter plots the first match.
    if (filtering()
        && (selected_ < 0 || rows_[static_cast<size_t>(selected_)].kind != RowKind::Field))
        select_first_field();
}

// ─── Rows ────────────────────────────────────────────────────────────────────

bool DataSidebarModel::is_expanded(const std::string& id, bool default_expanded) const
{
    if (filtering())
        return true;
    const auto it = expanded_.find(id);
    return it != expanded_.end() ? it->second : default_expanded;
}

void DataSidebarModel::set_expanded(const std::string& row_id, bool expanded)
{
    expanded_[row_id] = expanded;
    rebuild_rows();
}

void DataSidebarModel::toggle_expanded(size_t row)
{
    if (row >= rows_.size() || !is_container(rows_[row].kind) || filtering())
        return;
    set_expanded(rows_[row].id, !rows_[row].expanded);
}

void DataSidebarModel::rebuild_rows()
{
    rows_.clear();
    match_count_      = 0;
    field_count_      = 0;
    source_count_     = 0;
    const bool filter = filtering();

    for (size_t p = 0; p < providers_.size(); ++p)
    {
        const DataProvider&            prov = *providers_[p];
        const std::vector<DataSource>& srcs = sources_[p];

        // Matching flags per field, and counts per source.
        std::vector<std::vector<char>> match(srcs.size());
        std::vector<size_t>            src_matches(srcs.size(), 0);
        size_t                         prov_matches = 0;
        for (size_t s = 0; s < srcs.size(); ++s)
        {
            match[s].resize(srcs[s].fields.size(), 0);
            for (size_t f = 0; f < srcs[s].fields.size(); ++f)
            {
                const bool m = field_matches_query(srcs[s].name, srcs[s].fields[f], query_);
                match[s][f]  = m ? 1 : 0;
                if (m)
                    ++src_matches[s];
            }
            prov_matches += src_matches[s];
            field_count_ += srcs[s].fields.size();
        }
        source_count_ += srcs.size();
        match_count_ += prov_matches;

        if (filter && prov_matches == 0)
            continue;

        const std::string prov_id = std::string("p:") + std::string(prov.id());
        DataRow           prow;
        prow.kind     = RowKind::Provider;
        prow.depth    = 0;
        prow.provider = p;
        prow.id       = prov_id;
        prow.label    = std::string(prov.title());
        prow.count    = filter ? prov_matches : [&]
        {
            size_t n = 0;
            for (const auto& s : srcs)
                n += s.fields.size();
            return n;
        }();
        prow.expanded = is_expanded(prov_id, true);
        rows_.push_back(prow);
        if (!prow.expanded)
            continue;

        if (srcs.empty())
        {
            DataRow e;
            e.kind     = RowKind::Empty;
            e.depth    = 1;
            e.provider = p;
            e.id       = prov_id + "/empty";
            e.label    = prov.empty_hint();
            rows_.push_back(std::move(e));
            continue;
        }

        for (size_t s = 0; s < srcs.size(); ++s)
        {
            const DataSource& src = srcs[s];
            if (filter && src_matches[s] == 0)
                continue;
            const std::string src_id = prov_id + "/s:" + std::to_string(src.key);
            DataRow           srow;
            srow.kind     = RowKind::Source;
            srow.depth    = 1;
            srow.provider = p;
            srow.source   = s;
            srow.id       = src_id;
            srow.label    = src.name;
            srow.detail   = src.detail;
            srow.count    = filter ? src_matches[s] : src.fields.size();
            srow.expanded = is_expanded(src_id, true);
            rows_.push_back(srow);
            if (!srow.expanded)
                continue;

            size_t f = 0;
            while (f < src.fields.size())
            {
                const std::string& group = src.fields[f].group;
                if (group.empty())
                {
                    if (!filter || match[s][f])
                    {
                        DataRow frow;
                        frow.kind     = RowKind::Field;
                        frow.depth    = 2;
                        frow.provider = p;
                        frow.source   = s;
                        frow.field    = f;
                        frow.id       = src_id + "/f:" + std::to_string(src.fields[f].key);
                        frow.label    = src.fields[f].name;
                        frow.detail   = src.fields[f].detail;
                        rows_.push_back(std::move(frow));
                    }
                    ++f;
                    continue;
                }

                // A run of fields sharing this group.
                size_t end = f;
                size_t n   = 0;
                while (end < src.fields.size() && src.fields[end].group == group)
                {
                    if (!filter || match[s][end])
                        ++n;
                    ++end;
                }
                if (n > 0)
                {
                    const std::string grp_id = src_id + "/g:" + group;
                    DataRow           grow;
                    grow.kind     = RowKind::Group;
                    grow.depth    = 2;
                    grow.provider = p;
                    grow.source   = s;
                    grow.field    = f;
                    grow.id       = grp_id;
                    grow.label    = group;
                    grow.count    = n;
                    grow.expanded = is_expanded(grp_id, false);
                    rows_.push_back(grow);
                    if (grow.expanded)
                    {
                        for (size_t k = f; k < end; ++k)
                        {
                            if (filter && !match[s][k])
                                continue;
                            DataRow frow;
                            frow.kind     = RowKind::Field;
                            frow.depth    = 3;
                            frow.provider = p;
                            frow.source   = s;
                            frow.field    = k;
                            frow.id       = src_id + "/f:" + std::to_string(src.fields[k].key);
                            frow.label    = src.fields[k].name;
                            frow.detail   = src.fields[k].detail;
                            rows_.push_back(std::move(frow));
                        }
                    }
                }
                f = end;
            }
        }
    }

    // Keep the selection on the same row id when it is still visible.
    selected_ = -1;
    if (!selected_id_.empty())
    {
        for (size_t i = 0; i < rows_.size(); ++i)
        {
            if (rows_[i].id == selected_id_)
            {
                selected_ = static_cast<int>(i);
                break;
            }
        }
    }
}

// ─── Selection ───────────────────────────────────────────────────────────────

void DataSidebarModel::select(size_t row)
{
    if (row >= rows_.size() || rows_[row].kind == RowKind::Empty)
        return;
    selected_    = static_cast<int>(row);
    selected_id_ = rows_[row].id;
}

void DataSidebarModel::clear_selection()
{
    selected_ = -1;
    selected_id_.clear();
}

void DataSidebarModel::select_first_field()
{
    for (size_t i = 0; i < rows_.size(); ++i)
    {
        if (rows_[i].kind == RowKind::Field)
        {
            select(i);
            return;
        }
    }
}

void DataSidebarModel::move_selection(int delta)
{
    if (rows_.empty() || delta == 0)
        return;
    const int n = static_cast<int>(rows_.size());
    if (selected_ < 0)
    {
        // First press selects the first (down) or last (up) selectable row.
        const int start = delta > 0 ? 0 : n - 1;
        const int step  = delta > 0 ? 1 : -1;
        for (int i = start; i >= 0 && i < n; i += step)
        {
            if (rows_[static_cast<size_t>(i)].kind != RowKind::Empty)
            {
                select(static_cast<size_t>(i));
                return;
            }
        }
        return;
    }

    const int step  = delta > 0 ? 1 : -1;
    int       left  = delta > 0 ? delta : -delta;
    int       i     = selected_;
    int       found = selected_;
    while (left > 0)
    {
        i += step;
        if (i < 0 || i >= n)
            break;
        if (rows_[static_cast<size_t>(i)].kind == RowKind::Empty)
            continue;
        found = i;
        --left;
    }
    select(static_cast<size_t>(found));
}

void DataSidebarModel::expand_selected()
{
    if (selected_ < 0)
        return;
    const DataRow& row = rows_[static_cast<size_t>(selected_)];
    if (!is_container(row.kind))
        return;
    if (!row.expanded)
    {
        set_expanded(row.id, true);
        return;
    }
    // Already open: step to the first child.
    const size_t next = static_cast<size_t>(selected_) + 1;
    if (next < rows_.size() && rows_[next].depth > row.depth && rows_[next].kind != RowKind::Empty)
        select(next);
}

void DataSidebarModel::collapse_selected()
{
    if (selected_ < 0)
        return;
    const DataRow& row = rows_[static_cast<size_t>(selected_)];
    if (is_container(row.kind) && row.expanded && !filtering())
    {
        set_expanded(row.id, false);
        return;
    }
    // Step to the parent row.
    for (int i = selected_ - 1; i >= 0; --i)
    {
        if (rows_[static_cast<size_t>(i)].depth < row.depth)
        {
            select(static_cast<size_t>(i));
            return;
        }
    }
}

PlotResult DataSidebarModel::activate_selected()
{
    if (selected_ < 0)
        return {false, "Nothing selected"};
    const size_t row = static_cast<size_t>(selected_);
    if (rows_[row].kind == RowKind::Field)
        return plot_row(row);
    toggle_expanded(row);
    return {true, {}};
}

// ─── Actions ─────────────────────────────────────────────────────────────────

void DataSidebarModel::set_status(std::string message, bool error)
{
    status_       = std::move(message);
    status_error_ = error;
}

PlotResult DataSidebarModel::plot_row(size_t row)
{
    return plot_row(row, current_target());
}

PlotResult DataSidebarModel::plot_row(size_t row, const PlotTarget& target)
{
    if (row >= rows_.size() || rows_[row].kind != RowKind::Field)
        return {false, "Not a field"};
    const DataRow&    r     = rows_[row];
    const DataSource& src   = sources_[r.provider][r.source];
    const DataField&  field = src.fields[r.field];
    return plot_field(r.provider, src.key, field.key, target);
}

PlotResult DataSidebarModel::plot_field(size_t            provider,
                                        uint64_t          source_key,
                                        uint64_t          field_key,
                                        const PlotTarget& target)
{
    PlotResult result;
    if (provider >= providers_.size())
        result = {false, "Unknown data source"};
    else
        result = providers_[provider]->plot_field(source_key, field_key, target);
    if (result.message.empty())
        result.message = result.ok ? "Plotted" : "Could not plot that field";
    set_status(result.message, !result.ok);
    return result;
}

PlotResult DataSidebarModel::plot_payload(const DataFieldDragPayload& payload,
                                          const PlotTarget&           target)
{
    return plot_field(payload.provider_index, payload.source_key, payload.field_key, target);
}

DragPayload DataSidebarModel::drag_payload(size_t row) const
{
    if (row >= rows_.size() || rows_[row].kind != RowKind::Field)
        return {};
    const DataRow&    r     = rows_[row];
    const DataSource& src   = sources_[r.provider][r.source];
    const DataField&  field = src.fields[r.field];
    DragPayload       out   = providers_[r.provider]->drag_payload(src.key, field.key);
    if (!out.empty())
        return out;

    DataFieldDragPayload p;
    p.provider_index = static_cast<uint32_t>(r.provider);
    p.source_key     = src.key;
    p.field_key      = field.key;
    out.type         = DATA_FIELD_DRAG_TYPE;
    out.data.resize(sizeof(p));
    std::memcpy(out.data.data(), &p, sizeof(p));
    return out;
}

}   // namespace spectra::ui::data_sidebar
