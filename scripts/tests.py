#!/usr/bin/env python3
from glitcher import Glitcher

dev = Glitcher()
test_values = {
    "holdoff": 1000,
    "duration": 500,
    "pattern": 0b1010101101,
    "trigger_edge": 0,
    "freq": int(250e6)
}

if not dev.set_holdoff(test_values["holdoff"]):
    print("Failed to set holdoff!")
if not dev.set_pattern(test_values["pattern"]):
    print("Failed to set pattern!")
if not dev.set_trigger_edge(test_values["trigger_edge"]):
    print("Failed to set trigger edge!")
if not dev.set_freq(test_values["freq"]):
    print("Failed to set frequency!")
settings = dev.get_settings()

# settings should match
if settings["holdoff"] != test_values["holdoff"] \
    or settings["pattern"] != test_values["pattern"] \
    or settings["edge"] != test_values["trigger_edge"] \
    or settings["freq"] != test_values["freq"]:
    print("Settings mismatch!")
    print("Expected:", test_values)
    print("Got:", settings)
else:
    print("[PASSED] set settings")

# duration should match
dev.set_duration(test_values["duration"])
settings = dev.get_settings()
if settings["pattern"] != test_values["duration"]:
    print("Duration mismatch!")
    print("Expected:", test_values["duration"])
    print("Got:", settings["duration"])
else:
    print("[PASSED] set duration")

# double arm should fail
dev.disarm()
dev.arm()
if dev.arm():
    print("Double arm should fail!")
else:
    print("[PASSED] double arm")

# should trigger when forced
dev.disarm()
dev.arm()
if dev.get_trigger_state():
    print("Trigger state should be 0 after arming!")
else:
    print("[PASSED] trigger state after arm")
dev.force_trigger()
if not dev.get_trigger_state():
    print("Did not trigger when forced!")
else:
    print("[PASSED] force trigger")
dev.close()
