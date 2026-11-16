// commands_tools.cpp — Tool mode and utility command descriptors.

#include "command_context.hpp"
#include "command_descriptor.hpp"

#include "ui/app/window_ui_context.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include "ui/theme/icons.hpp"
#endif

#ifdef SPECTRA_USE_GLFW
    #include "ui/input/input.hpp"
#endif

namespace spectra
{

std::vector<CommandDescriptor> make_tools_commands(CommandContext& ctx)
{
    std::vector<CommandDescriptor> cmds;

#ifdef SPECTRA_USE_IMGUI
    // ImGuiIntegration owns the active tool (the nav rail reads it and the
    // window loop mirrors it into the input handler every frame), so the
    // commands set it there; the input handler is updated right away too so
    // the tool applies before the next frame and without an ImGui UI.
    auto& ui_ctx      = ctx.ui_ctx;
    auto  select_tool = [&ui_ctx](ToolMode mode)
    {
        if (ui_ctx.imgui_ui)
            ui_ctx.imgui_ui->set_interaction_mode(mode);
        ui_ctx.input_handler.set_tool_mode(mode);
    };

    cmds.push_back({"tool.pan",
                    "Pan Tool",
                    "P",
                    "Tools",
                    static_cast<uint16_t>(ui::Icon::Hand),
                    [select_tool]() { select_tool(ToolMode::Pan); }});

    cmds.push_back({"tool.box_zoom",
                    "Box Zoom Tool",
                    "Z",
                    "Tools",
                    static_cast<uint16_t>(ui::Icon::ZoomIn),
                    [select_tool]() { select_tool(ToolMode::BoxZoom); }});

    cmds.push_back({"tool.select",
                    "Select Tool",
                    "S",
                    "Tools",
                    static_cast<uint16_t>(ui::Icon::Crosshair),
                    [select_tool]() { select_tool(ToolMode::Select); }});

    cmds.push_back({"tool.measure",
                    "Measure Tool",
                    "M",
                    "Tools",
                    static_cast<uint16_t>(ui::Icon::Ruler),
                    [select_tool]() { select_tool(ToolMode::Measure); }});

    cmds.push_back({"tool.annotate",
                    "Annotate Tool",
                    "N",
                    "Tools",
                    static_cast<uint16_t>(ui::Icon::Comment),
                    [select_tool]() { select_tool(ToolMode::Annotate); }});

    cmds.push_back({"tool.roi",
                    "ROI Tool",
                    "I",
                    "Tools",
                    static_cast<uint16_t>(ui::Icon::VectorSquare),
                    [select_tool]() { select_tool(ToolMode::ROI); }});
#else
    (void)ctx;
#endif

    return cmds;
}

}   // namespace spectra
