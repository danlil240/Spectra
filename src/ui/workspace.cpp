#include "workspace.hpp"

#include <plotix/figure.hpp>
#include <plotix/axes.hpp>
#include <plotix/series.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace plotix {

// ─── Simple JSON writer ──────────────────────────────────────────────────────

static std::string escape_json(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c; break;
        }
    }
    return out;
}

static void write_indent(std::ostringstream& os, int depth) {
    for (int i = 0; i < depth; ++i) os << "  ";
}

std::string Workspace::serialize_json(const WorkspaceData& data) {
    std::ostringstream os;
    os << "{\n";
    os << "  \"version\": " << data.version << ",\n";
    os << "  \"theme_name\": \"" << escape_json(data.theme_name) << "\",\n";
    os << "  \"active_figure_index\": " << data.active_figure_index << ",\n";

    // Panels
    os << "  \"panels\": {\n";
    os << "    \"inspector_visible\": " << (data.panels.inspector_visible ? "true" : "false") << ",\n";
    os << "    \"inspector_width\": " << data.panels.inspector_width << ",\n";
    os << "    \"nav_rail_expanded\": " << (data.panels.nav_rail_expanded ? "true" : "false") << "\n";
    os << "  },\n";

    // Figures
    os << "  \"figures\": [\n";
    for (size_t fi = 0; fi < data.figures.size(); ++fi) {
        const auto& fig = data.figures[fi];
        os << "    {\n";
        os << "      \"title\": \"" << escape_json(fig.title) << "\",\n";
        os << "      \"width\": " << fig.width << ",\n";
        os << "      \"height\": " << fig.height << ",\n";
        os << "      \"grid_rows\": " << fig.grid_rows << ",\n";
        os << "      \"grid_cols\": " << fig.grid_cols << ",\n";

        // Axes
        os << "      \"axes\": [\n";
        for (size_t ai = 0; ai < fig.axes.size(); ++ai) {
            const auto& ax = fig.axes[ai];
            os << "        {\n";
            os << "          \"x_min\": " << ax.x_min << ",\n";
            os << "          \"x_max\": " << ax.x_max << ",\n";
            os << "          \"y_min\": " << ax.y_min << ",\n";
            os << "          \"y_max\": " << ax.y_max << ",\n";
            os << "          \"auto_fit\": " << (ax.auto_fit ? "true" : "false") << ",\n";
            os << "          \"grid_visible\": " << (ax.grid_visible ? "true" : "false") << ",\n";
            os << "          \"x_label\": \"" << escape_json(ax.x_label) << "\",\n";
            os << "          \"y_label\": \"" << escape_json(ax.y_label) << "\",\n";
            os << "          \"title\": \"" << escape_json(ax.title) << "\"\n";
            os << "        }";
            if (ai + 1 < fig.axes.size()) os << ",";
            os << "\n";
        }
        os << "      ],\n";

        // Series
        os << "      \"series\": [\n";
        for (size_t si = 0; si < fig.series.size(); ++si) {
            const auto& s = fig.series[si];
            os << "        {\n";
            os << "          \"name\": \"" << escape_json(s.name) << "\",\n";
            os << "          \"type\": \"" << escape_json(s.type) << "\",\n";
            os << "          \"color_r\": " << s.color_r << ",\n";
            os << "          \"color_g\": " << s.color_g << ",\n";
            os << "          \"color_b\": " << s.color_b << ",\n";
            os << "          \"color_a\": " << s.color_a << ",\n";
            os << "          \"line_width\": " << s.line_width << ",\n";
            os << "          \"marker_size\": " << s.marker_size << ",\n";
            os << "          \"visible\": " << (s.visible ? "true" : "false") << ",\n";
            os << "          \"point_count\": " << s.point_count << "\n";
            os << "        }";
            if (si + 1 < fig.series.size()) os << ",";
            os << "\n";
        }
        os << "      ]\n";

        os << "    }";
        if (fi + 1 < data.figures.size()) os << ",";
        os << "\n";
    }
    os << "  ]\n";
    os << "}\n";

    return os.str();
}

// ─── Simple JSON reader ──────────────────────────────────────────────────────

// Minimal JSON parser — handles the specific format we write.
// Not a general-purpose JSON parser.

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

std::string Workspace::read_string_value(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";

    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return "";

    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return "";

    size_t end = pos + 1;
    while (end < json.size()) {
        if (json[end] == '"' && json[end - 1] != '\\') break;
        ++end;
    }
    return json.substr(pos + 1, end - pos - 1);
}

double Workspace::read_number_value(const std::string& json, const std::string& key, double default_val) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return default_val;

    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return default_val;

    // Skip whitespace
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;

    // Read number
    size_t end = pos;
    while (end < json.size() && (json[end] == '-' || json[end] == '.' ||
           json[end] == '+' || json[end] == 'e' || json[end] == 'E' ||
           (json[end] >= '0' && json[end] <= '9'))) ++end;

    if (end == pos) return default_val;
    try {
        return std::stod(json.substr(pos, end - pos));
    } catch (...) {
        return default_val;
    }
}

bool Workspace::read_bool_value(const std::string& json, const std::string& key, bool default_val) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return default_val;

    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return default_val;

    auto rest = trim(json.substr(pos + 1, 10));
    if (rest.substr(0, 4) == "true") return true;
    if (rest.substr(0, 5) == "false") return false;
    return default_val;
}

// Parse array of objects from JSON (finds [...] after key)
static std::vector<std::string> parse_json_array(const std::string& json, const std::string& key) {
    std::vector<std::string> objects;
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return objects;

    pos = json.find('[', pos);
    if (pos == std::string::npos) return objects;

    // Find matching objects within the array
    int depth = 0;
    size_t obj_start = 0;
    for (size_t i = pos + 1; i < json.size(); ++i) {
        if (json[i] == '{') {
            if (depth == 0) obj_start = i;
            ++depth;
        } else if (json[i] == '}') {
            --depth;
            if (depth == 0) {
                objects.push_back(json.substr(obj_start, i - obj_start + 1));
            }
        } else if (json[i] == ']' && depth == 0) {
            break;
        }
    }
    return objects;
}

bool Workspace::deserialize_json(const std::string& json, WorkspaceData& data) {
    data.version = static_cast<uint32_t>(read_number_value(json, "version", 1));
    if (data.version > WorkspaceData::FORMAT_VERSION) return false;

    data.theme_name = read_string_value(json, "theme_name");
    if (data.theme_name.empty()) data.theme_name = "dark";
    data.active_figure_index = static_cast<size_t>(read_number_value(json, "active_figure_index", 0));

    // Panels
    data.panels.inspector_visible = read_bool_value(json, "inspector_visible", true);
    data.panels.inspector_width = static_cast<float>(read_number_value(json, "inspector_width", 320.0));
    data.panels.nav_rail_expanded = read_bool_value(json, "nav_rail_expanded", false);

    // Figures
    auto fig_objects = parse_json_array(json, "figures");
    for (const auto& fig_json : fig_objects) {
        WorkspaceData::FigureState fig;
        fig.title = read_string_value(fig_json, "title");
        fig.width = static_cast<uint32_t>(read_number_value(fig_json, "width", 1280));
        fig.height = static_cast<uint32_t>(read_number_value(fig_json, "height", 720));
        fig.grid_rows = static_cast<int>(read_number_value(fig_json, "grid_rows", 1));
        fig.grid_cols = static_cast<int>(read_number_value(fig_json, "grid_cols", 1));

        // Axes
        auto ax_objects = parse_json_array(fig_json, "axes");
        for (const auto& ax_json : ax_objects) {
            WorkspaceData::AxisState ax;
            ax.x_min = static_cast<float>(read_number_value(ax_json, "x_min", 0));
            ax.x_max = static_cast<float>(read_number_value(ax_json, "x_max", 1));
            ax.y_min = static_cast<float>(read_number_value(ax_json, "y_min", 0));
            ax.y_max = static_cast<float>(read_number_value(ax_json, "y_max", 1));
            ax.auto_fit = read_bool_value(ax_json, "auto_fit", true);
            ax.grid_visible = read_bool_value(ax_json, "grid_visible", true);
            ax.x_label = read_string_value(ax_json, "x_label");
            ax.y_label = read_string_value(ax_json, "y_label");
            ax.title = read_string_value(ax_json, "title");
            fig.axes.push_back(std::move(ax));
        }

        // Series
        auto ser_objects = parse_json_array(fig_json, "series");
        for (const auto& ser_json : ser_objects) {
            WorkspaceData::SeriesState s;
            s.name = read_string_value(ser_json, "name");
            s.type = read_string_value(ser_json, "type");
            s.color_r = static_cast<float>(read_number_value(ser_json, "color_r", 1));
            s.color_g = static_cast<float>(read_number_value(ser_json, "color_g", 1));
            s.color_b = static_cast<float>(read_number_value(ser_json, "color_b", 1));
            s.color_a = static_cast<float>(read_number_value(ser_json, "color_a", 1));
            s.line_width = static_cast<float>(read_number_value(ser_json, "line_width", 2));
            s.marker_size = static_cast<float>(read_number_value(ser_json, "marker_size", 6));
            s.visible = read_bool_value(ser_json, "visible", true);
            s.point_count = static_cast<size_t>(read_number_value(ser_json, "point_count", 0));
            fig.series.push_back(std::move(s));
        }

        data.figures.push_back(std::move(fig));
    }

    return true;
}

// ─── Save / Load ─────────────────────────────────────────────────────────────

bool Workspace::save(const std::string& path, const WorkspaceData& data) {
    std::string json = serialize_json(data);
    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << json;
    return file.good();
}

bool Workspace::load(const std::string& path, WorkspaceData& data) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::ostringstream ss;
    ss << file.rdbuf();
    std::string json = ss.str();

    if (json.empty()) return false;
    return deserialize_json(json, data);
}

// ─── Capture / Apply ─────────────────────────────────────────────────────────

WorkspaceData Workspace::capture(const std::vector<Figure*>& figures,
                                  size_t active_index,
                                  const std::string& theme_name,
                                  bool inspector_visible,
                                  float inspector_width,
                                  bool nav_rail_expanded) {
    WorkspaceData data;
    data.theme_name = theme_name;
    data.active_figure_index = active_index;
    data.panels.inspector_visible = inspector_visible;
    data.panels.inspector_width = inspector_width;
    data.panels.nav_rail_expanded = nav_rail_expanded;

    for (const auto* fig : figures) {
        if (!fig) continue;

        WorkspaceData::FigureState fs;
        fs.title = "";  // Figure has no title accessor yet
        fs.width = fig->width();
        fs.height = fig->height();
        fs.grid_rows = fig->grid_rows();
        fs.grid_cols = fig->grid_cols();

        for (const auto& ax : fig->axes()) {
            if (!ax) continue;
            WorkspaceData::AxisState as;
            auto xlim = ax->x_limits();
            auto ylim = ax->y_limits();
            as.x_min = xlim.min;
            as.x_max = xlim.max;
            as.y_min = ylim.min;
            as.y_max = ylim.max;
            as.grid_visible = ax->grid_enabled();
            as.x_label = ax->get_xlabel();
            as.y_label = ax->get_ylabel();
            as.title = ax->get_title();
            fs.axes.push_back(std::move(as));
        }

        for (const auto& ax : fig->axes()) {
            if (!ax) continue;
            for (const auto& s : ax->series()) {
                if (!s) continue;
                WorkspaceData::SeriesState ss;
                ss.name = s->label();
                ss.visible = s->visible();
                ss.color_r = s->color().r;
                ss.color_g = s->color().g;
                ss.color_b = s->color().b;
                ss.color_a = s->color().a;

                if (auto* ls = dynamic_cast<LineSeries*>(s.get())) {
                    ss.type = "line";
                    ss.line_width = ls->width();
                    ss.point_count = ls->x_data().size();
                } else if (auto* sc = dynamic_cast<ScatterSeries*>(s.get())) {
                    ss.type = "scatter";
                    ss.marker_size = sc->size();
                    ss.point_count = sc->x_data().size();
                }

                fs.series.push_back(std::move(ss));
            }
        }

        data.figures.push_back(std::move(fs));
    }

    return data;
}

bool Workspace::apply(const WorkspaceData& data,
                       std::vector<Figure*>& figures) {
    // Apply axis limits from workspace data to matching figures
    for (size_t fi = 0; fi < data.figures.size() && fi < figures.size(); ++fi) {
        const auto& fs = data.figures[fi];
        auto* fig = figures[fi];
        if (!fig) continue;

        for (size_t ai = 0; ai < fs.axes.size() && ai < fig->axes().size(); ++ai) {
            const auto& as = fs.axes[ai];
            auto* ax = fig->axes()[ai].get();
            if (!ax) continue;

            ax->xlim(as.x_min, as.x_max);
            ax->ylim(as.y_min, as.y_max);
        }
    }
    return true;
}

// ─── Paths ───────────────────────────────────────────────────────────────────

std::string Workspace::default_path() {
    const char* home = std::getenv("HOME");
    if (!home) home = std::getenv("USERPROFILE");
    if (!home) return "workspace.plotix";

    std::filesystem::path dir = std::filesystem::path(home) / ".config" / "plotix";
    std::filesystem::create_directories(dir);
    return (dir / "workspace.plotix").string();
}

std::string Workspace::autosave_path() {
    try {
        auto tmp = std::filesystem::temp_directory_path() / "plotix_autosave.plotix";
        return tmp.string();
    } catch (...) {
        return "plotix_autosave.plotix";
    }
}

} // namespace plotix
