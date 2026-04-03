#include <spectra/custom_series.hpp>

#include <cstring>

namespace spectra
{

CustomSeries::CustomSeries(const std::string& type_name) : type_name_(type_name) {}

void CustomSeries::set_data(const void* data, size_t byte_size, size_t element_count)
{
    data_.resize(byte_size);
    if (data && byte_size > 0)
    {
        std::memcpy(data_.data(), data, byte_size);
    }
    element_count_ = element_count;
    dirty_         = true;
}

void CustomSeries::set_bounds(float x_min, float x_max, float y_min, float y_max)
{
    bounds_x_min_ = x_min;
    bounds_x_max_ = x_max;
    bounds_y_min_ = y_min;
    bounds_y_max_ = y_max;
}

}   // namespace spectra
