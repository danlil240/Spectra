#pragma once

#include <cstddef>
#include <cstdint>
#include <spectra/series.hpp>
#include <string>
#include <vector>

namespace spectra
{

/// A series whose data format and rendering are defined by a plugin.
/// The type_name links to a SeriesTypeRegistry entry that holds the
/// upload/draw/bounds callbacks and the GPU pipeline.
class CustomSeries : public Series
{
   public:
    explicit CustomSeries(const std::string& type_name);

    const std::string& type_name() const { return type_name_; }

    /// Replace the raw data blob.  The byte layout is defined by the plugin.
    void set_data(const void* data, size_t byte_size, size_t element_count);

    const void* data() const { return data_.data(); }
    size_t      data_byte_size() const { return data_.size(); }
    size_t      element_count() const { return element_count_; }

    /// Plugin-computed bounds (set by the host after calling bounds_fn).
    void set_bounds(float x_min, float x_max, float y_min, float y_max);

    float bounds_x_min() const { return bounds_x_min_; }
    float bounds_x_max() const { return bounds_x_max_; }
    float bounds_y_min() const { return bounds_y_min_; }
    float bounds_y_max() const { return bounds_y_max_; }

    // Bring base-class getters into scope
    using Series::color;
    using Series::label;

    CustomSeries& label(const std::string& lbl)
    {
        Series::label(lbl);
        return *this;
    }
    CustomSeries& color(const Color& c)
    {
        Series::color(c);
        return *this;
    }

   private:
    std::string          type_name_;
    std::vector<uint8_t> data_;
    size_t               element_count_ = 0;
    float                bounds_x_min_  = 0.0f;
    float                bounds_x_max_  = 1.0f;
    float                bounds_y_min_  = 0.0f;
    float                bounds_y_max_  = 1.0f;
};

}   // namespace spectra
