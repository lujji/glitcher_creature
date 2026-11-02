#!/usr/bin/env python3
from glitcher import Glitcher, GlitcherError
from dma_compiler import compile_dma_sequence

dev = Glitcher()

dev.set_freq(250e6)

seq = [
    (0, 0b10101010101001001),
    (10, 0b11110111),
    (30, 0b11110111),
    (20, 0b1000101),
]

zeros_first, durations, patterns = compile_dma_sequence(seq)
print(f"zeros_first={zeros_first}, blocks={durations}, {len(patterns)} pattern words")
print("-" * 50)

dev.seq_init(zeros_first)
dev.seq_push_duration(*durations)
dev.seq_push_pattern(*patterns)

print('finished uploading sequence, executing!')

# dev.seq_push_switcher(0, 10)
# dev.seq_push_switcher(0xffffffff, 0)
# dev.seq_push_switcher(0, 1)
# dev.seq_push_switcher(1, 0)
# dev.seq_push_switcher(0, 0)
# dev.seq_push_switcher(1, 10)
# dev.seq_push_switcher(0, 10)

switcher_sequence = [
    (1 << 0, 15),
    (0, 0),
    (1 << 1, 17),
    (0, 0),
    (1 << 2, 27),
    (0, 0),
    (1 << 3, 22),
    (0, 0),
]

for i in switcher_sequence:
    dev.seq_push_switcher(*i)

for i in range(1):
    dev.force_trigger()

exit(0)

# protocol self-test
test_values = {
    "trigger_edges": 0,
    "rising_edge": True,
    "freq": int(250e6),
}

dev.set_trigger_edge(test_values["trigger_edges"])
dev.set_trigger_polarity(test_values["rising_edge"])
dev.set_freq(test_values["freq"])
settings = dev.get_settings()

# settings should match
if any(settings[k] != v for k, v in test_values.items()):
    print("Settings mismatch!")
    print("Expected:", test_values)
    print("Got:", settings)
else:
    print("[PASSED] set settings")

# double arm should fail
dev.disarm()
dev.arm()
try:
    dev.arm()
    print("Double arm should fail!")
except GlitcherError:
    print("[PASSED] double arm")

# should be ARMED after arming
dev.disarm()
dev.arm()
state = dev.get_trigger_state()
if state != Glitcher.State.ARMED:
    print("State should be ARMED after arming, got", state.name)
else:
    print("[PASSED] state after arm")

# should trigger when forced
dev.force_trigger()
if dev.get_trigger_state() != Glitcher.State.TRIGGERED:
    print("Did not trigger when forced!")
else:
    print("[PASSED] force trigger")
dev.close()
