#include "scene/scene_manager.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace spectra::adapters::ros2
{

namespace
{
std::string entity_selection_key(const SceneEntity& entity)
{
    return entity.type + '\n' + entity.display_name + '\n' + entity.topic + '\n' + entity.frame_id
           + '\n' + entity.label;
}

struct SceneBounds
{
    spectra::vec3 min{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
    };
    spectra::vec3 max{
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
    };
    bool valid{false};

    void include(const spectra::vec3& point)
    {
        min   = spectra::vec3_min(min, point);
        max   = spectra::vec3_max(max, point);
        valid = true;
    }

    void expand(double radius)
    {
        if (radius <= 0.0)
            return;
        const spectra::vec3 r{radius, radius, radius};
        min -= r;
        max += r;
    }
};

std::string property_as_string(const SceneEntity& entity,
                               const std::string& key,
                               const std::string& fallback = {})
{
    const auto it =
        std::find_if(entity.properties.begin(),
                     entity.properties.end(),
                     [&](const SceneProperty& property) { return property.key == key; });
    return it == entity.properties.end() ? fallback : it->value;
}

SceneBounds entity_bounds(const SceneEntity& entity)
{
    SceneBounds bounds;
    bounds.include(entity.transform.translation);

    const spectra::vec3 half_scale{
        std::max(0.025, std::abs(entity.scale.x) * 0.5),
        std::max(0.025, std::abs(entity.scale.y) * 0.5),
        std::max(0.025, std::abs(entity.scale.z) * 0.5),
    };
    bounds.include(entity.transform.translation - half_scale);
    bounds.include(entity.transform.translation + half_scale);

    if (entity.type == "grid")
    {
        const double      cell_size   = std::max(0.05, std::abs(entity.scale.x));
        const double      cell_count  = std::max(1.0, std::abs(entity.scale.z));
        const double      half_extent = cell_size * cell_count * 0.5;
        const std::string plane       = property_as_string(entity, "plane", "xz");
        if (plane == "xy")
        {
            bounds.include(entity.transform.translation
                           + spectra::vec3{-half_extent, -half_extent, -0.05});
            bounds.include(entity.transform.translation
                           + spectra::vec3{half_extent, half_extent, 0.05});
        }
        else if (plane == "yz")
        {
            bounds.include(entity.transform.translation
                           + spectra::vec3{-0.05, -half_extent, -half_extent});
            bounds.include(entity.transform.translation
                           + spectra::vec3{0.05, half_extent, half_extent});
        }
        else
        {
            bounds.include(entity.transform.translation
                           + spectra::vec3{-half_extent, -0.05, -half_extent});
            bounds.include(entity.transform.translation
                           + spectra::vec3{half_extent, 0.05, half_extent});
        }
    }

    if (entity.polyline.has_value())
    {
        for (const auto& point : entity.polyline->points)
            bounds.include(entity.transform.transform_point(point));
        bounds.expand(0.05);
    }

    if (entity.arrow.has_value())
    {
        const auto&         arrow  = *entity.arrow;
        const spectra::vec3 origin = entity.transform.transform_point(arrow.origin);
        const spectra::vec3 direction =
            spectra::vec3_normalize(entity.transform.transform_vector(arrow.direction));
        const spectra::vec3 tip = origin + direction * arrow.shaft_length;
        bounds.include(origin);
        bounds.include(tip);
        bounds.expand(std::max({0.05, arrow.head_width * 0.5, arrow.head_length * 0.25}));
    }

    if (entity.billboard.has_value())
    {
        const double half_w = std::max(0.05, entity.billboard->width * 0.5);
        const double half_h = std::max(0.05, entity.billboard->height * 0.5);
        bounds.include(entity.transform.translation + spectra::vec3{-half_w, -half_h, -0.05});
        bounds.include(entity.transform.translation + spectra::vec3{half_w, half_h, 0.05});
    }

    if (entity.point_set.has_value())
    {
        for (const auto& point : entity.point_set->points)
            bounds.include(entity.transform.transform_point(point.position));
        bounds.expand(0.05);
    }

    return bounds;
}

bool ray_intersects_aabb(const spectra::Ray& ray, const SceneBounds& bounds, double& t_out)
{
    if (!bounds.valid)
        return false;

    double t_min = 0.0;
    double t_max = std::numeric_limits<double>::infinity();

    for (int axis = 0; axis < 3; ++axis)
    {
        const double origin    = ray.origin[axis];
        const double direction = ray.direction[axis];
        const double slab_min  = bounds.min[axis];
        const double slab_max  = bounds.max[axis];

        if (std::abs(direction) < 1e-12)
        {
            if (origin < slab_min || origin > slab_max)
                return false;
            continue;
        }

        double t1 = (slab_min - origin) / direction;
        double t2 = (slab_max - origin) / direction;
        if (t1 > t2)
            std::swap(t1, t2);

        t_min = std::max(t_min, t1);
        t_max = std::min(t_max, t2);
        if (t_min > t_max)
            return false;
    }

    t_out = t_min;
    return true;
}
}   // namespace

void SceneManager::clear()
{
    entities_.clear();
    selected_index_.reset();
}

size_t SceneManager::add_entity(SceneEntity entity)
{
    const size_t index = entities_.size();
    if (selected_key_.has_value() && entity_selection_key(entity) == *selected_key_)
        selected_index_ = index;
    entities_.push_back(std::move(entity));
    return index;
}

size_t SceneManager::entity_count() const
{
    return entities_.size();
}

const std::vector<SceneEntity>& SceneManager::entities() const
{
    return entities_;
}

std::optional<size_t> SceneManager::pick(const spectra::Ray& ray) const
{
    std::optional<size_t> best_index;
    double                best_t = std::numeric_limits<double>::infinity();

    for (size_t i = 0; i < entities_.size(); ++i)
    {
        const SceneBounds bounds = entity_bounds(entities_[i]);
        double            t      = 0.0;
        if (!ray_intersects_aabb(ray, bounds, t))
            continue;
        if (t < best_t)
        {
            best_t     = t;
            best_index = i;
        }
    }

    return best_index;
}

void SceneManager::set_selected_index(std::optional<size_t> index)
{
    if (!index.has_value())
    {
        selected_index_.reset();
        selected_key_.reset();
        return;
    }
    if (*index >= entities_.size())
        return;
    selected_index_ = index;
    selected_key_   = entity_selection_key(entities_[*index]);
}

void SceneManager::clear_selection()
{
    selected_index_.reset();
    selected_key_.reset();
}

std::optional<size_t> SceneManager::selected_index() const
{
    return selected_index_;
}

const SceneEntity* SceneManager::selected_entity() const
{
    if (!selected_index_.has_value() || *selected_index_ >= entities_.size())
        return nullptr;
    return &entities_[*selected_index_];
}

}   // namespace spectra::adapters::ros2
