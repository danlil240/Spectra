// handlers_figure.cpp — create_figure, add_series, switch_figure,
//                       get_figure_info, get_state handlers.

#include "../automation_handler.hpp"
#include "../automation_json.hpp"
#include "../automation_server.hpp"

#include <spectra/app.hpp>
#include <spectra/axes.hpp>
#include <spectra/axes3d.hpp>
#include <spectra/figure.hpp>
#include <spectra/figure_registry.hpp>
#include <spectra/series.hpp>
#include <spectra/series_stats.hpp>

#include "ui/app/window_ui_context.hpp"
#include "ui/figures/figure_manager.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include "ui/theme/theme.hpp"
#endif

#include <cmath>
#include <sstream>

namespace spectra
{

std::vector<AutomationHandlerEntry> make_figure_handlers()
{
    std::vector<AutomationHandlerEntry> entries;

    // ── get_state ────────────────────────────────────────────────────────
    entries.push_back(
        {"get_state",
         [](AutomationRequest& req, App& app, WindowUIContext* ui_ctx)
         {
             std::ostringstream oss;
             oss << "{";
             auto ids = app.figure_registry().all_ids();
             oss << "\"figure_count\":" << ids.size();

             FigureId active_id = INVALID_FIGURE_ID;
#ifdef SPECTRA_USE_IMGUI
             if (ui_ctx && ui_ctx->fig_mgr)
                 active_id = ui_ctx->fig_mgr->active_index();
#endif
             oss << ",\"active_figure_id\":" << active_id;

             oss << ",\"figures\":[";
             for (size_t i = 0; i < ids.size(); ++i)
             {
                 if (i > 0)
                     oss << ",";
                 Figure* fig = app.figure_registry().get(ids[i]);
                 oss << "{\"id\":" << ids[i];
                 if (fig)
                 {
                     oss << ",\"width\":" << fig->width() << ",\"height\":" << fig->height()
                         << ",\"axes_count\":" << fig->axes().size();
                     size_t ts = 0;
                     for (const auto& ax : fig->axes())
                     {
                         if (ax)
                             ts += ax->series().size();
                     }
                     oss << ",\"total_series\":" << ts;
                 }
                 oss << "}";
             }
             oss << "]";

#ifdef SPECTRA_USE_IMGUI
             if (ui_ctx)
             {
                 oss << ",\"undo_count\":" << ui_ctx->undo_mgr.undo_count()
                     << ",\"redo_count\":" << ui_ctx->undo_mgr.redo_count() << ",\"is_3d_mode\":"
                     << ((ui_ctx->fig_mgr && ui_ctx->fig_mgr->active_state().is_in_3d_mode())
                             ? "true"
                             : "false")
                     << ",\"theme\":\""
                     << json_escape(ui_ctx->theme_mgr ? ui_ctx->theme_mgr->current_theme_name()
                                                      : "")
                     << '"';
             }
#else
             (void)ui_ctx;
#endif
             oss << "}";
             req.response_json = json_ok(req.id, oss.str());
         }});

    // ── create_figure ────────────────────────────────────────────────────
    entries.push_back(
        {"create_figure",
         [](AutomationRequest& req, App& app, WindowUIContext* /*ui_ctx*/)
         {
             uint32_t w       = static_cast<uint32_t>(json_get_int(req.params_json, "width", 1280));
             uint32_t h       = static_cast<uint32_t>(json_get_int(req.params_json, "height", 720));
             Figure&  new_fig = app.figure({w, h});
             FigureId new_id  = app.figure_registry().find_id(&new_fig);
             req.response_json = json_ok(req.id, "{\"figure_id\":" + std::to_string(new_id) + "}");
         }});

    // ── add_series ───────────────────────────────────────────────────────
    entries.push_back(
        {"add_series",
         [](AutomationRequest& req, App& app, WindowUIContext* /*ui_ctx*/)
         {
             uint64_t    fig_id = json_get_uint64(req.params_json, "figure_id", 0);
             std::string type   = json_get_string(req.params_json, "type");
             if (type.empty())
                 type = "line";
             int n_points = json_get_int(req.params_json, "n_points", 100);

             Figure* fig = app.figure_registry().get(static_cast<FigureId>(fig_id));
             if (!fig)
             {
                 req.response_json = json_error(req.id, "Figure not found");
                 return;
             }

             if (fig->axes().empty())
                 fig->subplot(1, 1, 1);
             auto& ax = *fig->axes_mut()[0];

             std::vector<float> x_caller = json_get_float_array(req.params_json, "x");
             std::vector<float> y_caller = json_get_float_array(req.params_json, "y");

             std::vector<float> x_gen, y_gen;
             if (x_caller.empty() || y_caller.empty())
             {
                 x_gen.resize(static_cast<size_t>(n_points));
                 y_gen.resize(static_cast<size_t>(n_points));
                 for (int i = 0; i < n_points; ++i)
                 {
                     x_gen[i] = static_cast<float>(i);
                     y_gen[i] = std::sin(static_cast<float>(i) * 0.1f);
                 }
             }

             const std::vector<float>& x = x_caller.empty() ? x_gen : x_caller;
             const std::vector<float>& y = y_caller.empty() ? y_gen : y_caller;

             std::string label   = json_get_string(req.params_json, "label");
             Series*     new_ser = nullptr;
             if (type == "scatter")
                 new_ser = &ax.scatter(x, y);
             else if (type == "bar")
                 new_ser = &ax.bar(x, y);
             else if (type == "histogram")
                 new_ser = &ax.histogram(y, json_get_int(req.params_json, "bins", 30));
             else
                 new_ser = &ax.line(x, y);
             if (new_ser && !label.empty())
                 new_ser->label(label);

             req.response_json =
                 json_ok(req.id, "{\"series_count\":" + std::to_string(ax.series().size()) + "}");
         }});

    // ── switch_figure ────────────────────────────────────────────────────
    entries.push_back({"switch_figure",
                       [](AutomationRequest& req, App& /*app*/, WindowUIContext* ui_ctx)
                       {
#ifdef SPECTRA_USE_IMGUI
                           if (!ui_ctx || !ui_ctx->fig_mgr)
                           {
                               req.response_json = json_error(req.id, "No UI");
                               return;
                           }
                           uint64_t fig_id = json_get_uint64(req.params_json, "figure_id", 0);
                           ui_ctx->fig_mgr->queue_switch(static_cast<FigureId>(fig_id));
                           req.response_json = json_ok(req.id);
#else
                           (void)ui_ctx;
                           req.response_json = json_error(req.id, "ImGui not available");
#endif
                       }});

    // ── get_figure_info ──────────────────────────────────────────────────
    entries.push_back(
        {"get_figure_info",
         [](AutomationRequest& req, App& app, WindowUIContext* /*ui_ctx*/)
         {
             uint64_t fig_id = json_get_uint64(req.params_json, "figure_id", 0);
             Figure*  fig    = app.figure_registry().get(static_cast<FigureId>(fig_id));
             if (!fig)
             {
                 req.response_json = json_error(req.id, "Figure not found");
                 return;
             }

             std::ostringstream oss;
             oss << "{\"figure_id\":" << fig_id << ",\"width\":" << fig->width()
                 << ",\"height\":" << fig->height() << ",\"axes_count\":" << fig->axes().size()
                 << ",\"all_axes_count\":" << fig->all_axes().size();

             // 2D axes details
             oss << ",\"axes\":[";
             for (size_t ai = 0; ai < fig->axes().size(); ++ai)
             {
                 if (ai > 0)
                     oss << ",";
                 auto* ax = fig->axes()[ai].get();
                 if (!ax)
                 {
                     oss << "null";
                     continue;
                 }
                 auto xl = ax->x_limits();
                 auto yl = ax->y_limits();
                 oss << "{\"index\":" << ai << ",\"x_min\":" << xl.min << ",\"x_max\":" << xl.max
                     << ",\"y_min\":" << yl.min << ",\"y_max\":" << yl.max << ",\"series\":[";
                 for (size_t si = 0; si < ax->series().size(); ++si)
                 {
                     if (si > 0)
                         oss << ",";
                     auto* s = ax->series()[si].get();
                     if (!s)
                     {
                         oss << "null";
                         continue;
                     }
                     const char* stype  = "unknown";
                     size_t      scount = 0;
                     if (auto* ls = dynamic_cast<const LineSeries*>(s))
                     {
                         stype  = "line";
                         scount = ls->point_count();
                     }
                     else if (auto* ss = dynamic_cast<const ScatterSeries*>(s))
                     {
                         stype  = "scatter";
                         scount = ss->point_count();
                     }
                     else if (auto* bs = dynamic_cast<const BarSeries*>(s))
                     {
                         stype  = "bar";
                         scount = bs->bar_positions().size();
                     }
                     else if (auto* hs = dynamic_cast<const HistogramSeries*>(s))
                     {
                         stype  = "histogram";
                         scount = hs->bin_counts().size();
                     }
                     oss << "{\"label\":\"" << json_escape(s->label()) << "\",\"type\":\"" << stype
                         << "\",\"visible\":" << (s->visible() ? "true" : "false")
                         << ",\"point_count\":" << scount << "}";
                 }
                 oss << "]}";
             }
             oss << "]";

             // 3D axes
             oss << ",\"axes_3d\":[";
             size_t a3i = 0;
             for (size_t ai = 0; ai < fig->all_axes().size(); ++ai)
             {
                 auto* ax_base = fig->all_axes()[ai].get();
                 if (!ax_base)
                     continue;
                 auto* ax3d = dynamic_cast<Axes3D*>(ax_base);
                 if (!ax3d)
                     continue;
                 if (a3i > 0)
                     oss << ",";
                 auto xl = ax3d->x_limits();
                 auto yl = ax3d->y_limits();
                 auto zl = ax3d->z_limits();
                 oss << "{\"index\":" << ai << ",\"x_min\":" << xl.min << ",\"x_max\":" << xl.max
                     << ",\"y_min\":" << yl.min << ",\"y_max\":" << yl.max
                     << ",\"z_min\":" << zl.min << ",\"z_max\":" << zl.max
                     << ",\"series_count\":" << ax3d->series().size() << "}";
                 ++a3i;
             }
             oss << "]";

             oss << "}";
             req.response_json = json_ok(req.id, oss.str());
         }});

    return entries;
}

}   // namespace spectra
