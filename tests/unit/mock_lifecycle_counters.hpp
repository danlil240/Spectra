#pragma once

// Host-owned call counters shared by mock_lifecycle_plugin and
// test_plugin_lifecycle.  The plug-in writes them through a pointer the test
// hands it (mock_lifecycle_bind), so they stay readable after the library is
// unloaded: a counter that moves while the plug-in is unloaded is a
// use-after-unload.

#include <cstdint>

struct MockLifecycleCounters
{
    int32_t shutdown_calls  = 0;
    int32_t upload_calls    = 0;
    int32_t draw_calls      = 0;
    int32_t bounds_calls    = 0;
    int32_t cleanup_calls   = 0;
    int32_t transform_calls = 0;
    int32_t live_states     = 0;   // per-series GPU states allocated and not yet freed
    int32_t live_buffers    = 0;   // host buffers created through the C ABI, not yet destroyed
};

// Exported by the plug-in: void mock_lifecycle_bind(MockLifecycleCounters*).
using MockLifecycleBindFn = void (*)(MockLifecycleCounters*);
