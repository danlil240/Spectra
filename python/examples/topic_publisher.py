"""Example: publish data to a Spectra topic.

This runs whether or not a Spectra UI is open. When the UI later opens (or is
already open), the Topics panel will list the topic and a user can drag it
onto an axes to plot it live.
"""

import math
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "python"))

import spectra


def main() -> None:
    pub = spectra.Publisher("demo/sine", kind=spectra.Publisher.SCALAR_2D)
    print(f"Publishing on '{pub.name}'. Ctrl+C to stop.")

    hz = 50.0
    period = 1.0 / hz
    t = 0.0
    try:
        while True:
            pub.publish(t, math.sin(2 * math.pi * 1.0 * t))
            t += period
            time.sleep(period)
    except KeyboardInterrupt:
        pass
    finally:
        pub.close()


if __name__ == "__main__":
    main()
