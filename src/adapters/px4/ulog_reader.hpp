#pragma once

// The ULog parser lives in src/io/logs/ (namespace spectra::logs) so the main
// app can open ULog files through the generic LogReader interface without the
// PX4 adapter.  This header keeps the PX4 adapter's spelling of the names.

#include "io/logs/ulog_reader.hpp"

namespace spectra::adapters::px4
{

using spectra::logs::parse_ulog_type;
using spectra::logs::ulog_field_size;
using spectra::logs::ULogDataRow;
using spectra::logs::ULogDropout;
using spectra::logs::ULogField;
using spectra::logs::ULogFieldType;
using spectra::logs::ULogInfoMessage;
using spectra::logs::ULogLogMessage;
using spectra::logs::ULogMessageFormat;
using spectra::logs::ULogMetadata;
using spectra::logs::ULogParameter;
using spectra::logs::ULogReader;
using spectra::logs::ULogSubscription;
using spectra::logs::ULogTimeSeries;
using spectra::logs::ULogValue;

}   // namespace spectra::adapters::px4
