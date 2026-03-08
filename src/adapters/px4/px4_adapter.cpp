#include "px4_adapter.hpp"

#include <spectra/version.hpp>

namespace spectra::adapters::px4
{

const char* adapter_version()
{
    return SPECTRA_VERSION_STRING;
}

}   // namespace spectra::adapters::px4
