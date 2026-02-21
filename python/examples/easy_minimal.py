#!/usr/bin/env python3
"""The absolute simplest Spectra example. ONE line to plot.

Usage:
    python examples/easy_minimal.py
"""
import os
import sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import spectra as sp

# That's it. One line. Window opens with a plot.
sp.plot([1, 4, 9, 16, 25, 36, 49, 64, 81, 100])

sp.show()
