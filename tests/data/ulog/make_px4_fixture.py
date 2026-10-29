#!/usr/bin/env python3
"""Regenerates px4_fixture.ulg, the small PX4 ULog used by the PX4 viewer tests.

    python3 tests/data/ulog/make_px4_fixture.py

The file is committed; rerun this only when the fixture has to change, and
update the expectations in tests/unit/test_px4_ulog_fixture.cpp to match.

Contents (deterministic, little endian, ULog v1):
  * info:       sys_name="PX4", ver_hw="SITL"
  * parameters: 8 parameters in a deliberately unsorted file order, mixing
                float and int32 values, with two value ties (see PARAMS)
  * topics:     battery_status    (voltage_v)            10 samples
                sensor_combined   (gyro_rad[3])          10 samples
                vehicle_attitude  (q[4])                 10 samples
  * log:        three logging messages of different levels
"""

import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "px4_fixture.ulg")

# (name, type, value) in file order.  Ties: COM_ARM_WO_GPS == SYS_HAS_MAG == 1,
# MC_ROLL_P == MC_PITCH_P == 6.5.
PARAMS = [
    ("MC_ROLL_P", "float", 6.5),
    ("SYS_AUTOSTART", "int32_t", 4001),
    ("BAT_LOW_THR", "float", 0.15),
    ("COM_ARM_WO_GPS", "int32_t", 1),
    ("MPC_XY_VEL_MAX", "float", 12.0),
    ("SYS_HAS_MAG", "int32_t", 1),
    ("MC_PITCH_P", "float", 6.5),
    ("EKF2_GPS_DELAY", "float", 110.0),
]

SAMPLES = 10
T0_US = 1_000_000  # first sample at 1 s
DT_US = 100_000    # 10 Hz


def msg(kind: str, payload: bytes) -> bytes:
    return struct.pack("<HB", len(payload), ord(kind)) + payload


def key_value(type_and_key: str, value: bytes) -> bytes:
    k = type_and_key.encode()
    return struct.pack("<B", len(k)) + k + value


def main() -> None:
    out = bytearray()
    out += b"ULog\x01\x12\x35"           # magic
    out += struct.pack("<B", 1)            # version
    out += struct.pack("<Q", 1000)         # header timestamp (us)

    out += msg("I", key_value("char[3] sys_name", b"PX4"))
    out += msg("I", key_value("char[4] ver_hw", b"SITL"))

    for name, typ, value in PARAMS:
        raw = struct.pack("<f", value) if typ == "float" else struct.pack("<i", value)
        out += msg("P", key_value(f"{typ} {name}", raw))

    formats = [
        "battery_status:uint64_t timestamp;float voltage_v;",
        "sensor_combined:uint64_t timestamp;float[3] gyro_rad;",
        "vehicle_attitude:uint64_t timestamp;float[4] q;",
    ]
    for f in formats:
        out += msg("F", f.encode())

    topics = ["battery_status", "sensor_combined", "vehicle_attitude"]
    for msg_id, name in enumerate(topics):
        out += msg("A", struct.pack("<BH", 0, msg_id) + name.encode())

    for i in range(SAMPLES):
        ts = T0_US + i * DT_US
        # battery_status: 16.0 V falling by 0.1 V per sample.
        out += msg("D", struct.pack("<HQf", 0, ts, 16.0 - 0.1 * i))
        # sensor_combined: gyro = (i, -i, 0.5 * i) rad/s.
        out += msg("D", struct.pack("<HQ3f", 1, ts, float(i), float(-i), 0.5 * i))
        # vehicle_attitude: identity quaternion with a growing x component.
        out += msg("D", struct.pack("<HQ4f", 2, ts, 1.0, 0.01 * i, 0.0, 0.0))

    for level, ts, text in [
        (ord("6"), T0_US, "INFO  [commander] Ready for takeoff!"),
        (ord("4"), T0_US + 3 * DT_US, "WARN  [ekf2] GPS data stopped"),
        (ord("3"), T0_US + 7 * DT_US, "ERROR [battery] low battery"),
    ]:
        out += msg("L", struct.pack("<BQ", level, ts) + text.encode())

    with open(OUT, "wb") as fh:
        fh.write(out)
    print(f"wrote {OUT} ({len(out)} bytes)")


if __name__ == "__main__":
    main()
