#include "ros2_adapter.hpp"

#include <spectra/version.hpp>

namespace spectra::adapters::ros2
{

const char* adapter_version()
{
    return SPECTRA_VERSION_STRING;
}

}   // namespace spectra::adapters::ros2
