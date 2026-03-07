#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <spectra/math3d.hpp>

namespace spectra::adapters::ros2
{

struct SceneProperty
{
    std::string key;
    std::string value;
};

struct ScenePolyline
{
    std::vector<spectra::vec3> points;
    bool closed{false};
};

struct SceneArrow
{
    spectra::vec3 origin{};
    spectra::vec3 direction{1.0, 0.0, 0.0};
    double shaft_length{1.0};
    double head_length{0.2};
    double head_width{0.15};
};

struct SceneBillboard
{
    double width{1.0};
    double height{1.0};
};

struct ScenePoint
{
    spectra::vec3 position{};
    uint32_t rgba{0xFFFFFFFFu};
};

struct ScenePointSet
{
    std::vector<ScenePoint> points;
    float point_size{3.0f};
    uint32_t default_rgba{0xFFFFFFFFu};
    bool use_per_point_color{false};
    bool transparent{false};
};

struct SceneImage
{
    uint32_t width{0};
    uint32_t height{0};
    std::vector<uint8_t> rgba_data;
    uint64_t texture_id{0};
    bool needs_upload{false};
};

struct SceneEntity
{
    std::string type;
    std::string label;
    std::string display_name;
    std::string topic;
    std::string frame_id;
    spectra::Transform transform{};
    spectra::vec3 scale{1.0, 1.0, 1.0};
    uint64_t stamp_ns{0};
    std::optional<ScenePolyline> polyline;
    std::optional<SceneArrow> arrow;
    std::optional<SceneBillboard> billboard;
    std::optional<ScenePointSet> point_set;
    std::optional<SceneImage> image;
    std::vector<SceneProperty> properties;
};

class SceneManager
{
public:
    void clear();
    size_t add_entity(SceneEntity entity);
    size_t entity_count() const;
    const std::vector<SceneEntity>& entities() const;
    std::optional<size_t> pick(const spectra::Ray& ray) const;
    void set_selected_index(std::optional<size_t> index);
    void clear_selection();
    std::optional<size_t> selected_index() const;
    const SceneEntity* selected_entity() const;

private:
    std::vector<SceneEntity> entities_;
    std::optional<size_t> selected_index_;
    std::optional<std::string> selected_key_;
};

}   // namespace spectra::adapters::ros2
