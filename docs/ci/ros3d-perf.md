# ROS 3D performance budgets (Phase C4)

Reference thresholds for `bench_ros3d` on CI-style software Vulkan (lavapipe) and
developer desktops with discrete GPU.

## Targets

| Benchmark | Budget | Notes |
|-----------|--------|-------|
| `BM_PointCloudAdapter_Adapt/500000` | &lt; 25 ms | Decode + decimate 500k points |
| `BM_PointCloudDisplay_500kSubmit` | &lt; 33 ms | Scene entity build (30 FPS frame budget) |
| `BM_PointCloudDisplay_10HzBurst` | &lt; 330 ms total | Ten 500k frames @ 10 Hz |

Point cloud displays default to **500k** `max_points` with a visible **drop counter**
in the inspector when decimation occurs.

## Run locally

```bash
cmake --build build --target bench_ros3d
./build/tests/bench_ros3d --benchmark_filter=PointCloud
./build/tests/bench_ros3d --benchmark_filter=BM_PointCloudDisplay
```

## CI monitoring

The ROS2 workflow runs a smoke benchmark and fails on &gt;10% regression vs a
stored baseline when `SPECTRA_BENCH_BASELINE` is set. Default job uses
`--benchmark_min_time=0.05` for a fast gate.

## Tuning knobs

- **PointCloud display → Max Points** (inspector): caps GPU upload size.
- **Dropped points** counter: total points skipped by decimation since enable.
