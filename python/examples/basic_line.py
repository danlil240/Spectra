#!/usr/bin/env python3
"""Basic line plot example — Phase 1 verification.

Usage:
    # With backend already running:
    python examples/basic_line.py

    # Auto-launch backend (default):
    python examples/basic_line.py

This should open a window showing a sine wave plot.
"""

import math
import sys
import os

# Allow running from repo root without installing
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import spectra as sp

frequency = 5.0 # Hz
amplitude = 2.0
w = 2 * math.pi * frequency


sample_rate = 200.0 # Hz
sampling_time = 4.0 # Seconds

# Generate data: sine wave
n = int(sample_rate * sampling_time)
x = [i / sample_rate for i in range(n)]
y = [amplitude * math.sin(w * xi) for xi in x]

# Create session (auto-launches backend if needed)
s = sp.Session()

# Create figure with title
fig = s.figure("Sine Wave")

# Create axes (1x1 grid, index 1)
ax = fig.subplot(1, 1, 1)

# Add a line series
line = ax.line(x, y, label="sin(x)")

# Set axis labels and limits
ax.set_xlabel("x")
ax.set_ylabel("y")
ax.set_xlim(0.0, sampling_time)
ax.set_ylim(-amplitude - 0.5, amplitude + 0.5)
ax.grid(True)

# Show the figure (spawns a window agent)
fig.show()

print(f"Figure {fig.id} shown. Close the window to exit.")

# Block until all windows are closed
s.show()

# Clean up
s.close()
print("Done.")
