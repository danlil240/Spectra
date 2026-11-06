// data_source_topics.hpp — Publish plugin data sources as Spectra topics.
//
// DataSourceRegistry holds adapters registered by plugins (or the host).  The
// app polls them once per frame and hands the points to a TopicSink, so a
// plugin source shows up in the Topics panel like any other publisher and is
// plotted by dragging its topic onto an axes.  Each DataPoint becomes a
// Scalar2D sample (timestamp, value) on topic "<source name>/<series_label>".

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "daemon/bridges/topic_bridge.hpp"

namespace spectra
{

class DataSourceRegistry;

// Owner id under which plugin data-source topics are declared.
inline constexpr uint64_t DATA_SOURCE_TOPIC_OWNER_ID = daemon::BRIDGE_OWNER_ID_BASE + 0x100;

// "<source>/<label>", or just "<source>" when the label is empty.  Empty when
// the name is invalid (see daemon::join_topic_path).
std::string data_source_topic_name(std::string_view source, std::string_view label);

// Poll every running source once and push its points to `sink` (one batch
// per source).  Returns the number of points pushed.  UI thread only (the
// registry's poll contract).
size_t pump_data_sources_to_topics(DataSourceRegistry& registry,
                                   daemon::TopicSink&  sink,
                                   uint64_t            owner_id = DATA_SOURCE_TOPIC_OWNER_ID);

}   // namespace spectra
