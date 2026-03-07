#pragma once

#include "display_plugin.hpp"

namespace spectra::adapters::ros2
{

class GridDisplay : public DisplayPlugin
{
public:
    std::string type_id() const override { return "grid"; }
    std::string display_name() const override { return "Grid"; }
    std::string icon() const override { return "#"; }

    std::vector<std::string> compatible_message_types() const override { return {}; }

    std::string serialize_config_blob() const override;
    void deserialize_config_blob(const std::string& blob) override;
    void submit_renderables(SceneManager& scene) override;
    void draw_inspector_ui() override;

    float cell_size() const { return cell_size_; }
    int cell_count() const { return cell_count_; }
    const std::string& plane() const { return plane_; }

private:
    float cell_size_{1.0f};
    int   cell_count_{20};
    std::string plane_{"xz"};
    float color_[3]{0.45f, 0.45f, 0.45f};
    float alpha_{0.6f};
    float offset_[3]{0.0f, 0.0f, 0.0f};
};

}   // namespace spectra::adapters::ros2
