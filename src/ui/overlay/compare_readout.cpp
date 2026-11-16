#ifdef SPECTRA_USE_IMGUI

    #include "compare_readout.hpp"

    #include <algorithm>
    #include <cfloat>
    #include <cmath>
    #include <format>
    #include <imgui.h>
    #include <spectra/axes.hpp>
    #include <spectra/figure.hpp>
    #include <string>
    #include <vector>

    #include "axis_readout.hpp"
    #include "crosshair_mapping.hpp"
    #include "overlay_text_fit.hpp"
    #include "ui/data/axis_link.hpp"
    #include "ui/input/input.hpp"
    #include "ui/theme/design_tokens.hpp"
    #include "ui/theme/theme.hpp"

namespace spectra
{

namespace
{

ImU32 to_u32(const ui::Color& c, float alpha)
{
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, c.a * alpha));
}

ImU32 to_u32(const Color& c, float alpha)
{
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, alpha));
}

std::string series_display_name(const Series& s, int index)
{
    if (!s.label().empty())
        return s.label();
    return "Series " + std::to_string(index + 1);
}

}   // namespace

bool CompareReadout::draw(const CursorReadout& cursor,
                          Figure&              figure,
                          Axes&                hovered,
                          AxisLinkManager*     links,
                          float                opacity,
                          float                window_width,
                          float                window_height,
                          ImDrawList*          dl)
{
    if (!enabled_ || !theme_mgr_ || !cursor.valid || opacity < 0.01f)
        return false;

    const double data_x = crosshair_mapping::screen_to_data_x(hovered, cursor.screen_x);
    const auto   cx     = static_cast<float>(cursor.screen_x);
    const auto   cy     = static_cast<float>(cursor.screen_y);

    bool drawn = draw_for_axes(hovered, data_x, cx, cy, opacity, window_width, window_height, dl);

    // X-linked axes share the X: same markers and a readout at the same X.
    if (links)
    {
        std::vector<Axes*> peers;
        for (const LinkGroupId gid : links->groups_for(&hovered))
        {
            const LinkGroup* g = links->group(gid);
            if (!g || !has_flag(g->axis, LinkAxis::X))
                continue;
            for (Axes* m : g->members)
            {
                if (m && m != &hovered && std::find(peers.begin(), peers.end(), m) == peers.end())
                    peers.push_back(m);
            }
        }
        for (Axes* peer : peers)
        {
            // Only peers shown in this figure.
            const auto& all = figure.axes();
            if (std::none_of(all.begin(),
                             all.end(),
                             [peer](const auto& p) { return p.get() == peer; }))
                continue;
            const Rect& vp = peer->viewport();
            if (vp.w <= 0.0f || vp.h <= 0.0f)
                continue;
            const auto sx = static_cast<float>(crosshair_mapping::data_to_screen_x(*peer, data_x));
            if (sx < vp.x || sx > vp.x + vp.w)
                continue;
            drawn |= draw_for_axes(*peer,
                                   data_x,
                                   sx,
                                   vp.y + vp.h * 0.5f,
                                   opacity,
                                   window_width,
                                   window_height,
                                   dl);
        }
    }
    return drawn;
}

bool CompareReadout::draw_for_axes(const Axes& axes,
                                   double      data_x,
                                   float       anchor_x,
                                   float       anchor_y,
                                   float       opacity,
                                   float       window_width,
                                   float       window_height,
                                   ImDrawList* dl)
{
    const std::vector<probe::CompareRow> rows = probe::compare_at_x(axes, data_x, mode_, sort_);
    if (rows.empty())
        return false;

    const auto&       colors = theme_mgr_->colors();
    ImDrawList*       fg     = dl ? dl : ImGui::GetForegroundDrawList();
    const Rect&       vp     = axes.viewport();
    const AxisMapping map(axes);

    // ── Markers: each series' point at X ────────────────────────────────
    constexpr float DOT_R = 4.0f;
    for (const auto& r : rows)
    {
        if (!std::isfinite(r.sample.y) || !map.x_drawable(r.sample.x)
            || !map.y_drawable(r.sample.y))
            continue;
        const auto px = static_cast<float>(map.to_screen_x(r.sample.x));
        const auto py = static_cast<float>(map.to_screen_y(r.sample.y));
        if (px < vp.x - 0.5f || px > vp.x + vp.w + 0.5f || py < vp.y - 0.5f
            || py > vp.y + vp.h + 0.5f)
            continue;
        fg->AddCircleFilled(ImVec2(px, py), DOT_R + 1.5f, to_u32(colors.bg_canvas, opacity), 16);
        fg->AddCircleFilled(ImVec2(px, py), DOT_R, to_u32(r.series->color(), opacity), 16);
    }

    // ── Text content ────────────────────────────────────────────────────
    ImFont*     font    = font_body_ ? font_body_ : ImGui::GetFont();
    const float font_sz = font->LegacySize * 0.92f;
    auto        measure = [&](std::string_view s)
    { return font->CalcTextSizeA(font_sz, FLT_MAX, 0.0f, s.data(), s.data() + s.size()).x; };

    const size_t shown = std::min(rows.size(), MAX_ROWS);
    const size_t more  = rows.size() - shown;

    // Same digits as the crosshair's X label (what one pixel resolves).
    const std::string header =
        "X  "
        + ui::format_axis_cursor(&axes,
                                 ui::ReadoutAxis::X,
                                 data_x,
                                 crosshair_mapping::pixel_resolution_x(axes, anchor_x),
                                 24);

    constexpr float PAD        = 8.0f;
    constexpr float SWATCH     = 8.0f;
    constexpr float SWATCH_GAP = 6.0f;
    constexpr float COL_GAP    = 14.0f;
    constexpr float MAX_NAME_W = 180.0f;
    const float     row_h      = font_sz + 5.0f;

    std::vector<std::string> names(shown);
    std::vector<std::string> values(shown);
    float                    name_w  = 0.0f;
    float                    value_w = 0.0f;
    for (size_t i = 0; i < shown; ++i)
    {
        const auto& r = rows[i];
        names[i]      = ui::overlay_fit::ellipsize(series_display_name(*r.series, r.series_index),
                                              MAX_NAME_W,
                                              measure);
        values[i]     = ui::format_axis_value(&axes, ui::ReadoutAxis::Y, r.sample.y);
        name_w        = std::max(name_w, measure(names[i]));
        value_w       = std::max(value_w, measure(values[i]));
    }
    const std::string more_text = more > 0 ? std::format("+{} more", more) : std::string();

    float content_w = SWATCH + SWATCH_GAP + name_w + COL_GAP + value_w;
    content_w       = std::max(content_w, measure(header));
    if (more > 0)
        content_w = std::max(content_w, measure(more_text));
    const float divider_h = 7.0f;
    const float box_w     = content_w + PAD * 2.0f;
    const float box_h     = PAD * 2.0f + row_h + divider_h + row_h * static_cast<float>(shown)
                        + (more > 0 ? row_h : 0.0f);

    // Right of the line; flip left when it would leave the plot (or window).
    constexpr float OFFSET = 16.0f;
    const float     right  = std::min(vp.x + vp.w, window_width);
    float           bx     = anchor_x + OFFSET;
    if (bx + box_w > right)
        bx = anchor_x - OFFSET - box_w;
    bx       = std::clamp(bx,
                    ui::overlay_fit::WINDOW_MARGIN,
                    std::max(ui::overlay_fit::WINDOW_MARGIN,
                             window_width - box_w - ui::overlay_fit::WINDOW_MARGIN));
    float by = anchor_y - box_h * 0.5f;
    by       = std::clamp(by,
                    std::max(vp.y, ui::overlay_fit::WINDOW_MARGIN),
                    std::max(std::max(vp.y, ui::overlay_fit::WINDOW_MARGIN),
                             std::min(vp.y + vp.h, window_height) - box_h));

    // ── Box ─────────────────────────────────────────────────────────────
    const float rnd = ui::tokens::RADIUS_MD;
    fg->AddRectFilled(ImVec2(bx, by),
                      ImVec2(bx + box_w, by + box_h),
                      to_u32(colors.tooltip_bg, opacity),
                      rnd);
    fg->AddRect(ImVec2(bx, by),
                ImVec2(bx + box_w, by + box_h),
                to_u32(colors.tooltip_border, opacity),
                rnd,
                0,
                0.75f);

    const float ox      = bx + PAD;
    float       oy      = by + PAD;
    const float text_dy = (row_h - font_sz) * 0.5f;

    fg->AddText(font,
                font_sz,
                ImVec2(ox, oy + text_dy),
                to_u32(colors.text_secondary, opacity),
                header.c_str());
    oy += row_h;
    fg->AddLine(ImVec2(ox, oy + divider_h * 0.5f),
                ImVec2(ox + content_w, oy + divider_h * 0.5f),
                to_u32(colors.border_subtle, 0.5f * opacity),
                0.75f);
    oy += divider_h;

    for (size_t i = 0; i < shown; ++i)
    {
        const auto& r  = rows[i];
        const float sy = oy + (row_h - SWATCH) * 0.5f;
        fg->AddRectFilled(ImVec2(ox, sy),
                          ImVec2(ox + SWATCH, sy + SWATCH),
                          to_u32(r.series->color(), opacity),
                          2.0f);
        fg->AddText(font,
                    font_sz,
                    ImVec2(ox + SWATCH + SWATCH_GAP, oy + text_dy),
                    to_u32(colors.text_secondary, opacity),
                    names[i].c_str());
        // Values right-aligned so the digits line up.
        const float vw = measure(values[i]);
        fg->AddText(font,
                    font_sz,
                    ImVec2(ox + content_w - vw, oy + text_dy),
                    to_u32(colors.text_primary, opacity),
                    values[i].c_str());
        oy += row_h;
    }
    if (more > 0)
    {
        fg->AddText(font,
                    font_sz,
                    ImVec2(ox, oy + text_dy),
                    to_u32(colors.text_tertiary, opacity),
                    more_text.c_str());
    }
    return true;
}

}   // namespace spectra

#endif   // SPECTRA_USE_IMGUI
