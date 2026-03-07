#pragma once

#include <string>
#include <vector>

#include <spectra/math3d.hpp>

#include "display_plugin.hpp"

namespace spectra::adapters::ros2
{

class TfDisplay : public DisplayPlugin
{
public:
    std::string type_id() const override { return "tf"; }
    std::string display_name() const override { return "TF"; }
    std::string icon() const override { return "TF"; }

    void on_enable(const DisplayContext& context) override;
    void on_disable() override;
    void on_update(float dt) override;
    void submit_renderables(SceneManager& scene) override;
    void draw_inspector_ui() override;

    std::vector<std::string> compatible_message_types() const override
    {
        return {"tf2_msgs/msg/TFMessage"};
    }

    std::string serialize_config_blob() const override;
    void deserialize_config_blob(const std::string& blob) override;

    size_t frame_visual_count() const { return frames_.size(); }

private:
    struct FrameVisual
    {
        std::string frame_id;
        std::string parent_frame_id;
        spectra::Transform transform{};
        double hz{0.0};
        uint64_t age_ms{0};
        bool is_static{false};
        bool stale{false};
    };

    const TfBuffer* tf_buffer_{nullptr};
    std::string fixed_frame_;
    std::vector<FrameVisual> frames_;
    bool show_labels_{true};
    float axis_scale_{0.25f};
    int max_frames_{256};
};

}   // namespace spectra::adapters::ros2
