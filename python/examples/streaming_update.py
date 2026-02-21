#!/usr/bin/env python3
"""Streaming data update example — demonstrates Series.append() and FramePacer.

Simulates a live sensor stream: appends new data points at 30 FPS.

Usage:
    python streaming_update.py
"""

import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import spectra as sp

# Create figure and axes
s = sp.Session()
fig = s.figure("Streaming Demo", width=1024, height=600)
ax = fig.subplot(1, 1, 1)
ax.set_xlabel("Time (s)")
ax.set_ylabel("Value")
ax.set_title("Live Sensor Stream")

# Create two series
line1 = ax.line([], [], label="sin")
line2 = ax.line([], [], label="cos")

# Show the figure
fig.show()

# Stream data using FramePacer
pacer = sp.FramePacer(fps=30)
t = 0.0
dt_step = 1.0 / 30.0
window_sec = 10.0  # visible time window (seconds)

ax.set_ylim(-1.2, 1.2)
ax.set_xlim(0.0, window_sec)

print("Streaming data at 30 FPS. Close the window to stop.")

while fig.is_visible:
    t += dt_step
    x_new = [t]
    y1_new = [math.sin(t * 2.0)]
    y2_new = [math.cos(t * 2.0) * 0.5]

    line1.append(x_new, y1_new)
    line2.append(x_new, y2_new)

    # Slide x-axis to follow the stream
    if t > window_sec:
        ax.set_xlim(t - window_sec, t)

    pacer.pace(s)

s.close()
print("Done.")
