#include "ui/displays_panel.hpp"

#include <algorithm>

#include "topic_discovery.hpp"

#ifdef SPECTRA_USE_IMGUI
    #include <imgui.h>
#endif

namespace spectra::adapters::ros2
{

namespace
{

std::vector<TopicInfo> matching_topics(const DisplayPlugin& display, const DisplayContext& context)
{
    if (!context.topic_discovery)
        return {};

    const auto compatible_types = display.compatible_message_types();
    if (compatible_types.empty())
        return {};

    auto                   topics = context.topic_discovery->topics();
    std::vector<TopicInfo> matches;
    matches.reserve(topics.size());
    for (const auto& topic : topics)
    {
        if (topic.types.empty())
            continue;
        const auto& type_name = topic.types.front();
        if (std::find(compatible_types.begin(), compatible_types.end(), type_name)
            != compatible_types.end())
        {
            matches.push_back(topic);
        }
    }

    std::sort(matches.begin(),
              matches.end(),
              [](const TopicInfo& a, const TopicInfo& b) { return a.name < b.name; });
    return matches;
}

const char* type_leaf(const std::string& type_name)
{
    const auto slash = type_name.rfind('/');
    return slash == std::string::npos ? type_name.c_str() : type_name.c_str() + slash + 1;
}

void seed_topic_if_possible(DisplayPlugin& display, const DisplayContext& context)
{
    if (!display.topic().empty())
        return;

    const auto matches = matching_topics(display, context);
    if (!matches.empty())
        display.set_topic(matches.front().name);
}

bool draw_topic_selector(DisplayPlugin& display, const DisplayContext& context)
{
    const auto matches = matching_topics(display, context);
    if (matches.empty())
        return false;

    std::string current_label = display.topic().empty() ? "(choose topic)" : display.topic();
    if (ImGui::BeginCombo("Topic", current_label.c_str()))
    {
        const bool none_selected = display.topic().empty();
        if (ImGui::Selectable("(none)", none_selected))
            display.set_topic("");
        if (none_selected)
            ImGui::SetItemDefaultFocus();

        for (const auto& topic : matches)
        {
            const bool  selected = display.topic() == topic.name;
            std::string label    = topic.name;
            if (!topic.types.empty())
                label += "  [" + std::string(type_leaf(topic.types.front())) + "]";
            if (ImGui::Selectable(label.c_str(), selected))
                display.set_topic(topic.name);
            if (selected)
                ImGui::SetItemDefaultFocus();
        }

        ImGui::EndCombo();
    }
    return true;
}

void add_display_instance(const DisplayRegistry&                       registry,
                          const DisplayContext&                        context,
                          std::vector<std::unique_ptr<DisplayPlugin>>& displays,
                          int&                                         selected_index,
                          const std::string&                           type_id)
{
    if (auto display = registry.create(type_id))
    {
        seed_topic_if_possible(*display, context);
        if (display->enabled())
            display->on_enable(context);
        displays.push_back(std::move(display));
        selected_index = static_cast<int>(displays.size()) - 1;
    }
}

}   // namespace

void DisplaysPanel::draw(bool*                                        p_open,
                         const DisplayRegistry&                       registry,
                         const DisplayContext&                        context,
                         std::vector<std::unique_ptr<DisplayPlugin>>& displays)
{
#ifdef SPECTRA_USE_IMGUI
    if (!ImGui::GetCurrentContext())
        return;
    if (!ImGui::Begin(title_.c_str(), p_open))
    {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Add Display"))
        ImGui::OpenPopup("##add_display_popup");
    ImGui::SameLine();
    const bool can_remove =
        selected_index_ >= 0 && selected_index_ < static_cast<int>(displays.size());
    if (ImGui::Button("Remove Selected") && can_remove)
    {
        displays[static_cast<size_t>(selected_index_)]->on_destroy();
        displays.erase(displays.begin() + selected_index_);
        if (selected_index_ >= static_cast<int>(displays.size()))
            selected_index_ = static_cast<int>(displays.size()) - 1;
    }

    if (ImGui::BeginPopup("##add_display_popup"))
    {
        const auto                          display_types = registry.list_types();
        std::vector<const DisplayTypeInfo*> suggested;
        std::vector<const DisplayTypeInfo*> other;
        suggested.reserve(display_types.size());
        other.reserve(display_types.size());

        if (context.topic_discovery)
        {
            const auto topics = context.topic_discovery->topics();
            for (const auto& type : display_types)
            {
                bool has_match = false;
                for (const auto& topic : topics)
                {
                    if (topic.types.empty())
                        continue;
                    if (std::find(type.compatible_types.begin(),
                                  type.compatible_types.end(),
                                  topic.types.front())
                        != type.compatible_types.end())
                    {
                        has_match = true;
                        break;
                    }
                }
                (has_match ? suggested : other).push_back(&type);
            }
        }
        else
        {
            for (const auto& type : display_types)
                other.push_back(&type);
        }

        auto draw_popup_entry = [&](const DisplayTypeInfo& type)
        {
            std::string label = type.display_name;
            if (!type.icon.empty())
                label = type.icon + std::string("  ") + label;
            if (ImGui::Selectable(label.c_str()))
            {
                add_display_instance(registry, context, displays, selected_index_, type.type_id);
            }
        };

        if (!suggested.empty())
        {
            ImGui::TextDisabled("Suggested From Topics");
            ImGui::Separator();
            for (const auto* type : suggested)
                draw_popup_entry(*type);
            if (!other.empty())
            {
                ImGui::Spacing();
                ImGui::TextDisabled("Other Displays");
                ImGui::Separator();
            }
        }

        for (const auto* type : other)
            draw_popup_entry(*type);

        ImGui::EndPopup();
    }

    ImGui::Separator();
    for (int index = 0; index < static_cast<int>(displays.size()); ++index)
    {
        auto& display = displays[static_cast<size_t>(index)];
        ImGui::PushID(index);

        bool enabled = display->enabled();
        if (ImGui::Checkbox("##enabled", &enabled))
        {
            if (enabled)
                display->on_enable(context);
            else
                display->on_disable();
            display->set_enabled(enabled);
        }

        ImGui::SameLine();
        std::string label = display->display_name();
        if (!display->topic().empty())
            label += " [" + display->topic() + "]";
        if (ImGui::Selectable(label.c_str(), selected_index_ == index))
            selected_index_ = index;
        if (display->status() != DisplayStatus::Disabled)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", display->status_text().c_str());
        }

        ImGui::PopID();
    }

    const bool has_selection =
        selected_index_ >= 0 && selected_index_ < static_cast<int>(displays.size());
    if (has_selection)
    {
        ImGui::Separator();
        auto& selected_display = *displays[static_cast<size_t>(selected_index_)];
        if (!selected_display.compatible_message_types().empty())
        {
            const bool has_selector = draw_topic_selector(selected_display, context);
            if (!has_selector)
                ImGui::TextDisabled("No compatible discovered topics yet.");
            ImGui::Separator();
        }
        selected_display.draw_inspector_ui();
    }

    ImGui::End();
#else
    (void)p_open;
    (void)registry;
    (void)context;
    (void)displays;
#endif
}

}   // namespace spectra::adapters::ros2
