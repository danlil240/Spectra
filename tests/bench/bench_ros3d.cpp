// Phase 6 ROS3D benchmarks — TfBuffer lookup, point cloud adaptation,
// marker batch ingestion, and scene manager operations.

#include <benchmark/benchmark.h>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include <sensor_msgs/msg/laser_scan.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>
#include <sensor_msgs/msg/point_field.hpp>
#include <visualization_msgs/msg/marker.hpp>

#include "display/marker_display.hpp"
#include "display/pointcloud_display.hpp"
#include "messages/laserscan_adapter.hpp"
#include "messages/marker_adapter.hpp"
#include "messages/pointcloud_adapter.hpp"
#include "scene/scene_manager.hpp"
#include "tf/tf_buffer.hpp"

using namespace spectra::adapters::ros2;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static TransformStamp make_ts(const std::string& parent,
                              const std::string& child,
                              double tx,
                              uint64_t recv_ns,
                              bool is_static = false)
{
    TransformStamp ts;
    ts.parent_frame = parent;
    ts.child_frame = child;
    ts.tx = tx;
    ts.qw = 1.0;
    ts.recv_ns = recv_ns;
    ts.is_static = is_static;
    return ts;
}

static sensor_msgs::msg::PointCloud2 make_cloud(size_t n)
{
    sensor_msgs::msg::PointCloud2 msg;
    msg.header.frame_id = "lidar";
    msg.header.stamp.sec = 0;
    msg.header.stamp.nanosec = 100;
    msg.height = 1;
    msg.width = static_cast<uint32_t>(n);
    msg.point_step = 16;
    msg.row_step = msg.point_step * msg.width;
    msg.is_bigendian = false;
    msg.fields.resize(4);
    msg.fields[0].name = "x";
    msg.fields[0].offset = 0;
    msg.fields[0].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[0].count = 1;
    msg.fields[1].name = "y";
    msg.fields[1].offset = 4;
    msg.fields[1].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[1].count = 1;
    msg.fields[2].name = "z";
    msg.fields[2].offset = 8;
    msg.fields[2].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[2].count = 1;
    msg.fields[3].name = "intensity";
    msg.fields[3].offset = 12;
    msg.fields[3].datatype = sensor_msgs::msg::PointField::FLOAT32;
    msg.fields[3].count = 1;
    msg.data.resize(msg.row_step);

    for (size_t i = 0; i < n; ++i)
    {
        const float t = static_cast<float>(i) * 0.01f;
        float vals[4] = {std::cos(t), std::sin(t), t * 0.1f, t};
        std::memcpy(msg.data.data() + i * msg.point_step, vals, sizeof(vals));
    }
    return msg;
}

static sensor_msgs::msg::LaserScan make_scan(size_t n)
{
    sensor_msgs::msg::LaserScan msg;
    msg.header.frame_id = "laser";
    msg.header.stamp.sec = 0;
    msg.header.stamp.nanosec = 100;
    msg.angle_min = 0.0f;
    msg.angle_increment = static_cast<float>(2.0 * M_PI) / static_cast<float>(n);
    msg.range_min = 0.0f;
    msg.range_max = 100.0f;
    msg.ranges.resize(n);
    msg.intensities.resize(n);
    for (size_t i = 0; i < n; ++i)
    {
        msg.ranges[i] = 1.0f + static_cast<float>(i % 10) * 0.1f;
        msg.intensities[i] = static_cast<float>(i);
    }
    return msg;
}

// ---------------------------------------------------------------------------
// TfBuffer benchmarks
// ---------------------------------------------------------------------------

static void BM_TfBuffer_Insert(benchmark::State& state)
{
    const auto n = static_cast<size_t>(state.range(0));
    std::vector<TransformStamp> stamps;
    stamps.reserve(n);
    for (size_t i = 0; i < n; ++i)
    {
        stamps.push_back(make_ts("world", "frame_" + std::to_string(i),
                                 static_cast<double>(i), i * 1000));
    }

    for (auto _ : state)
    {
        TfBuffer buffer;
        for (const auto& ts : stamps)
            buffer.inject_transform(ts);
        benchmark::DoNotOptimize(buffer);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(n));
}
BENCHMARK(BM_TfBuffer_Insert)->Arg(10)->Arg(100)->Arg(1000);

static void BM_TfBuffer_Lookup_Chain(benchmark::State& state)
{
    const auto depth = static_cast<size_t>(state.range(0));
    TfBuffer buffer;
    // Build a linear chain: world -> f0 -> f1 -> ... -> f_{depth-1}
    buffer.inject_transform(make_ts("world", "f0", 1.0, 100));
    for (size_t i = 1; i < depth; ++i)
    {
        buffer.inject_transform(
            make_ts("f" + std::to_string(i - 1), "f" + std::to_string(i), 1.0, 100));
    }

    const std::string leaf = "f" + std::to_string(depth - 1);
    for (auto _ : state)
    {
        auto result = buffer.lookup_transform(leaf, "world");
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_TfBuffer_Lookup_Chain)->Arg(2)->Arg(5)->Arg(10)->Arg(20)->Arg(50);

static void BM_TfBuffer_AllFrames(benchmark::State& state)
{
    const auto n = static_cast<size_t>(state.range(0));
    TfBuffer buffer;
    for (size_t i = 0; i < n; ++i)
    {
        buffer.inject_transform(make_ts("world", "frame_" + std::to_string(i),
                                        static_cast<double>(i), i * 1000));
    }

    for (auto _ : state)
    {
        auto frames = buffer.all_frames();
        benchmark::DoNotOptimize(frames);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(n));
}
BENCHMARK(BM_TfBuffer_AllFrames)->Arg(10)->Arg(100)->Arg(500);

// ---------------------------------------------------------------------------
// Point cloud adapter benchmarks
// ---------------------------------------------------------------------------

static void BM_PointCloudAdapter_Adapt(benchmark::State& state)
{
    const auto n = static_cast<size_t>(state.range(0));
    const auto cloud = make_cloud(n);
    for (auto _ : state)
    {
        auto frame = adapt_pointcloud_message(cloud, "/points", n);
        benchmark::DoNotOptimize(frame);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(n));
}
BENCHMARK(BM_PointCloudAdapter_Adapt)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(500000);

static void BM_PointCloudAdapter_Decimate(benchmark::State& state)
{
    const auto cloud = make_cloud(500000);
    const auto max_pts = static_cast<size_t>(state.range(0));
    for (auto _ : state)
    {
        auto frame = adapt_pointcloud_message(cloud, "/points", max_pts);
        benchmark::DoNotOptimize(frame);
    }
    state.SetItemsProcessed(state.iterations() * 500000);
}
BENCHMARK(BM_PointCloudAdapter_Decimate)->Arg(10000)->Arg(50000)->Arg(100000);

// ---------------------------------------------------------------------------
// Laser scan adapter benchmarks
// ---------------------------------------------------------------------------

static void BM_LaserScanAdapter_Adapt(benchmark::State& state)
{
    const auto n = static_cast<size_t>(state.range(0));
    const auto scan = make_scan(n);
    for (auto _ : state)
    {
        auto frame = adapt_laserscan_message(scan, "/scan");
        benchmark::DoNotOptimize(frame);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(n));
}
BENCHMARK(BM_LaserScanAdapter_Adapt)->Arg(360)->Arg(720)->Arg(2048);

// ---------------------------------------------------------------------------
// Marker batch benchmarks
// ---------------------------------------------------------------------------

static void BM_MarkerDisplay_BatchIngest(benchmark::State& state)
{
    const auto n = static_cast<size_t>(state.range(0));
    std::vector<MarkerData> markers;
    markers.reserve(n);
    for (size_t i = 0; i < n; ++i)
    {
        MarkerData m;
        m.topic = "/markers";
        m.ns = "bench";
        m.id = static_cast<int32_t>(i);
        m.action = visualization_msgs::msg::Marker::ADD;
        m.primitive = MarkerPrimitive::Sphere;
        m.frame_id = "world";
        m.scale = {0.1, 0.1, 0.1};
        m.color = {1.0f, 0.0f, 0.0f, 1.0f};
        markers.push_back(m);
    }

    for (auto _ : state)
    {
        MarkerDisplay display;
        DisplayContext context;
        context.fixed_frame = "world";
        display.on_enable(context);

        for (const auto& m : markers)
            display.ingest_marker_data(m);
        display.on_update(0.016f);

        SceneManager scene;
        display.submit_renderables(scene);
        benchmark::DoNotOptimize(scene);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(n));
}
BENCHMARK(BM_MarkerDisplay_BatchIngest)->Arg(10)->Arg(100)->Arg(500)->Arg(1000);

// ---------------------------------------------------------------------------
// SceneManager benchmarks
// ---------------------------------------------------------------------------

static void BM_SceneManager_AddEntities(benchmark::State& state)
{
    const auto n = static_cast<size_t>(state.range(0));
    for (auto _ : state)
    {
        SceneManager scene;
        for (size_t i = 0; i < n; ++i)
        {
            SceneEntity entity;
            entity.type = "marker";
            entity.label = "entity_" + std::to_string(i);
            entity.frame_id = "world";
            entity.transform.translation = {
                static_cast<double>(i), 0.0, 0.0};
            scene.add_entity(std::move(entity));
        }
        benchmark::DoNotOptimize(scene);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(n));
}
BENCHMARK(BM_SceneManager_AddEntities)->Arg(10)->Arg(100)->Arg(500)->Arg(1000);

static void BM_SceneManager_Pick(benchmark::State& state)
{
    const auto n = static_cast<size_t>(state.range(0));
    SceneManager scene;
    for (size_t i = 0; i < n; ++i)
    {
        SceneEntity entity;
        entity.type = "marker";
        entity.label = "entity_" + std::to_string(i);
        entity.frame_id = "world";
        entity.transform.translation = {
            static_cast<double>(i) * 2.0, 0.0, 0.0};
        entity.scale = {1.0, 1.0, 1.0};
        scene.add_entity(std::move(entity));
    }

    spectra::Ray ray;
    ray.origin = {static_cast<double>(n), 10.0, 0.0};
    ray.direction = {0.0, -1.0, 0.0};

    for (auto _ : state)
    {
        auto result = scene.pick(ray);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SceneManager_Pick)->Arg(10)->Arg(100)->Arg(500)->Arg(1000);
