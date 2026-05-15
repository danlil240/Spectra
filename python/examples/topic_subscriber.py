"""Example: subscribe to a topic and plot it live.

Run alongside ``examples/topic_publisher`` (or any publisher) to see the data
appear live in a plot.
"""

import sys
import time
import os

# Make `import spectra` work from a source checkout when not pip-installed.
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "python"))

import spectra
from spectra import _codec as codec, _protocol as P


def main() -> None:
    topic_name = sys.argv[1] if len(sys.argv) > 1 else "demo/sine"

    # Open a session, create a figure with one axes.
    sess = spectra.Session()
    fig = sess.figure()
    ax = fig.subplot(1, 1, 1)
    fig.show()

    # Subscribe via raw request (no high-level API yet — Phase 1).
    # Sentinel series_index=0xFFFFFFFF asks the daemon to auto-create a series.
    payload = codec.encode_req_subscribe_topic(topic_name, fig._id, 0)
    reply = sess._request(P.REQ_SUBSCRIBE_TOPIC, payload)
    info = codec.decode_resp_subscribe_topic(reply["payload"])
    print(f"Subscribed: {info}")

    # Block until the figure window closes.
    sess.show()


if __name__ == "__main__":
    main()
