#!/usr/bin/env python3
"""Live streaming dashboard — real-time data with zero boilerplate.

Demonstrates sp.live() which runs your callback in a background thread.
The window updates automatically at the specified FPS.

Usage:
    python examples/easy_live_dashboard.py
"""

import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import spectra as sp

# ─── Example 1: Simplest possible live plot ───────────────────────────────────
# Just return a number from the callback — it auto-plots!
sp.title("Live Sine Wave")
sp.xlabel("Time (s)")
sp.ylabel("Value")
sp.live(lambda t: math.sin(t * 2.0), fps=30)

# ─── Block until window closed ───────────────────────────────────────────────
print("Live streaming at 30 FPS. Close the window to exit.")
sp.show()
print("Done.")
