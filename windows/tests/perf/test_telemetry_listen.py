"""Offline ABI checks for telemetry_listen.py; no socket, game, or shared mapping.

    py -3 -B test_telemetry_listen.py
"""
import struct
import telemetry_listen as listener


def packet(length, dash, steer, sentinel=""):
    data = bytearray(length)
    struct.pack_into("<iI", data, 0, 1, 0x12345678)
    struct.pack_into("<f", data, 16, 3210.0)
    struct.pack_into("<f", data, 20, 0.0)
    struct.pack_into("<f", data, dash + 12, 24.5)
    data[dash + 71:dash + 73] = bytes((200, 15))
    data[dash + 75] = 3
    struct.pack_into("<b", data, dash + 76, steer)
    if sentinel:
        data[323] = ord(sentinel)
    return bytes(data)


assert struct.calcsize(listener.SHM_FORMAT) == 76
horizon = listener.decode(packet(324, 244, -64, "R"))
assert horizon == dict(raceOn=1, packetMs=0x12345678, speed=24.5, gear=3, accel=200,
                       brake=15, steer=-64, accX=0.0, rpm=3210.0, sentinel="R")
fm7 = listener.decode(packet(311, 232, 127))
assert fm7 == {**horizon, "steer": 127, "sentinel": ""}
assert listener.decode(bytes(323)) is None
assert listener.decode(bytes(232)) is None
print("telemetry listener: Horizon/FM7 offsets, signed steering and 76-byte shared ABI passed")
