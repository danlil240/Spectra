#!/usr/bin/env python3
"""Multi-signal live streaming — multiple series updating in real-time.

Shows how to use sp.live() with manual series control for multiple signals.

Usage:
    python examples/easy_multi_live.py
"""

import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import spectra as sp

# Create figure and series
sp.title("Multi-Signal Live Stream")
sp.xlabel("Time (s)")
sp.ylabel("Amplitude")
sp.grid()

# Pre-create series we'll update
sin_line = sp.plot([], [], color="red", label="sin(2t)")
cos_line = sp.plot([], [], color="blue", label="cos(3t)")
saw_line = sp.plot([], [], color="green", label="sawtooth")
sp.legend()

# Buffers for sliding window
t_buf = []
sin_buf = []
cos_buf = []
saw_buf = []
WINDOW = 300  # 10 seconds at 30 FPS


def update(t, dt):
    t_buf.append(t)
    sin_buf.append(math.sin(t * 2.0))
    cos_buf.append(math.cos(t * 3.0) * 0.7)
    saw_buf.append((t % 2.0) - 1.0)

    # Sliding window
    if len(t_buf) > WINDOW:
        del t_buf[0], sin_buf[0], cos_buf[0], saw_buf[0]

    sin_line.set_data(t_buf, sin_buf)
    cos_line.set_data(t_buf, cos_buf)
    saw_line.set_data(t_buf, saw_buf)

    # Auto-scroll x axis
    if len(t_buf) > 1:
        sp.xlim(t_buf[0], t_buf[-1])
    sp.ylim(-1.5, 1.5)


sp.live(update, fps=30)

print("Streaming 3 signals at 30 FPS. Close the window to exit.")
sp.show()
print("Done.")
