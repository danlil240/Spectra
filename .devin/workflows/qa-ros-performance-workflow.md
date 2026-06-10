---
description: QA ROS Performance Review - spectra-ros workflow validation, report triage, and regression verification
---

> **All instructions, scenario tables, file maps, and triage rules are consolidated in:**
> `skills/qa-ros-performance-agent/SKILL.md`
>
> Follow the workflow defined there. This file is kept as a workflow entry point only.

## Quick Start

// turbo
1. Build ROS QA agent:
```bash
cmake -S . -B build-ros2 -G Ninja -DSPECTRA_USE_ROS2=ON -DSPECTRA_BUILD_QA_AGENT=ON && cmake --build build-ros2 --target spectra_ros_qa_agent -j$(nproc)
```

2. Inspect available scenarios:
```bash
./build-ros2/tests/spectra_ros_qa_agent --list-scenarios
```

3. Run deterministic baseline:
```bash
env ROS_LOG_DIR=/tmp/ros_logs ROS_HOME=/tmp/spectra_ros_home \
  ./build-ros2/tests/spectra_ros_qa_agent --seed 42 --duration 120 \
  --output-dir /tmp/spectra_ros_qa_$(date +%Y%m%d)
```

4. Read `qa_report.txt` and confirm full scenario coverage, not just the process exit code.

5. Update `skills/qa-ros-performance-agent/REPORT.md` and the relevant ROS plans after triage.
