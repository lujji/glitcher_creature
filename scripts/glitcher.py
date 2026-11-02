import serial, serial.tools.list_ports
import math
import struct
from enum import Enum, IntEnum, auto

from dma_compiler import compile_dma_sequence


class Glitch:
    '''
    Glitch sequence builder. Usage example:

        g = Glitch(250e6)
        g.push_glitch(g.us(39) + 16, g.pulse(42))  # 84 ns pulse at 39.032 us
        dev.load_glitch(g)
    '''

    def __init__(self, freq: float):
        self.freq = freq
        self.seq = []
        self.end = 0

        # time to PIO cycles
        self.ns = lambda t: math.floor(t * 2e-9 * freq + 0.5)
        self.us = lambda t: math.floor(t * 2e-6 * freq + 0.5)
        self.ms = lambda t: math.floor(t * 2e-3 * freq + 0.5)

    @staticmethod
    def pulse(width: int) -> int:
        '''
        Pulse of ones
        '''
        words = (width + 31) // 32
        return ((1 << width) - 1) << (32 * words - width)

    def push_glitch(self, cycle: int, pattern: int):
        '''
        Insert glitch pattern at requested cycle
        '''
        if pattern < 0:
            raise ValueError(f"negative pattern: {pattern}")

        if cycle < self.end:
            raise ValueError(f"glitch at {cycle} starts before the previous pattern ends at {self.end}")

        self.seq.append((cycle - self.end, pattern))
        self.end = cycle + 32 * max(1, (pattern.bit_length() + 31) // 32)


class GlitcherError(Exception):
    pass


class Glitcher:
    USB_VID = 0x16c0
    USB_PID = 0x05e1

    WORDS_PER_PACKET = 15
    MAX_BLOCKS = 102            # BITSTREAM_SEQ_LEN
    MAX_PATTERN_WORDS = 8192    # BITSTREAM_BUFFER_LEN
    ACK = 0xAA
    STATUS = {0x55: "NACK", 0x66: "unknown command", 0x77: "invalid length"}

    class CMD(Enum):
        ARM = auto(0x80),
        DISARM = auto(),
        FORCE_TRIGGER = auto(),
        SET_TRIGGER_EDGE = auto(),
        SET_TRIGGER_POLARITY = auto(),
        SEQ_INIT = auto(),
        SEQ_PUSH_DURATION = auto(),
        SEQ_PUSH_PATTERN = auto(),
        SEQ_PUSH_SWITCHER = auto(),
        SET_FREQ = auto(),
        GET_STATUS = auto(),
        GET_SETTINGS = auto(),
        GPIO_WRITE = auto(),
        GPIO_READ = auto(),
        GPIO_SET_PULL = auto(),
        REBOOT = auto(),

    class State(IntEnum):
        IDLE = 0
        ARMED = 1
        TRIGGERED = 2

    def __init__(self, port:str = None, ignore_ack:bool = False):
        '''
        Connect to device

        param port: Serial port to connect to (e.g. /dev/ttyACM0). If None, will try to find the device automatically
        param ignore_ack: YOLO mode - don't wait for ACK
        '''
        if port is None:
            for i in serial.tools.list_ports.comports():
                    if i.vid == self.USB_VID and i.pid == self.USB_PID and i.interface == "cli":
                        port = i.device
                        break
            else:
                raise GlitcherError("Device not found")
        self.ser = serial.Serial(port, timeout=0.5)
        self.ignore_ack = ignore_ack

    def _command(self, cmd: "Glitcher.CMD", fmt: str = "", *args, reply: int = 0) -> bytes:
        '''
        Send `cmd` with `args` packed as `fmt`, check the ACK and return `reply` bytes of response data.
        Raises GlitcherError if the device doesn't answer or rejects the command
        '''
        self.ser.reset_input_buffer()
        self.ser.write(struct.pack("<B" + fmt, cmd.value[0], *args))
        if self.ignore_ack and not reply:
            return b""

        ack = self.ser.read(2)
        if len(ack) < 2:
            raise GlitcherError(f"{cmd.name}: no reply")
        if ack[0] != cmd.value[0]:
            raise GlitcherError(f"{cmd.name}: got a reply for command {ack[0]:#04x}")
        if ack[1] != self.ACK:
            raise GlitcherError(f"{cmd.name}: {self.STATUS.get(ack[1], f'unknown status {ack[1]:#04x}')}")

        data = self.ser.read(reply)
        if len(data) < reply:
            raise GlitcherError(f"{cmd.name}: expected {reply} bytes of data, got {len(data)}")
        return data

    def _push_words(self, cmd: "Glitcher.CMD", words):
        for i in range(0, len(words), self.WORDS_PER_PACKET):
            chunk = words[i:i + self.WORDS_PER_PACKET]
            try:
                self._command(cmd, f"{len(chunk)}I", *chunk)
            except GlitcherError as e:
                raise GlitcherError(f"{e} (words {i}..{i + len(chunk) - 1} of {len(words)})") from None

    def arm(self):
        '''
        Arm the glitch module. Glitcher will fire when trigger condition is met.
        Fails if already armed or no valid sequence is uploaded
        '''
        self._command(self.CMD.ARM)

    def disarm(self):
        '''
        Disarm the glitcher
        '''
        self._command(self.CMD.DISARM)

    def force_trigger(self):
        '''
        Force the glitch module to trigger immediately. Fails if no valid sequence is uploaded
        '''
        self._command(self.CMD.FORCE_TRIGGER)

    def set_trigger_edge(self, edges: int):
        '''
        Trigger off Nth rising edge. 0 = 1st edge
        '''
        self._command(self.CMD.SET_TRIGGER_EDGE, "I", edges)

    def set_trigger_polarity(self, rising: bool):
        '''
        Set trigger polarity. True = trigger on rising edge, False = trigger on falling edge
        '''
        self._command(self.CMD.SET_TRIGGER_POLARITY, "B", bool(rising))

    def seq_init(self, zeros_first: bool):
        '''
        Initialize glitch sequence mode. If zeros_first is True the sequence starts with a 0 pattern
        '''
        self._command(self.CMD.SEQ_INIT, "B", bool(zeros_first))

    def seq_push_duration(self, *durations: int):
        '''
        Push one or more duration words
        '''
        self._push_words(self.CMD.SEQ_PUSH_DURATION, durations)

    def seq_push_pattern(self, *patterns: int):
        '''
        Push one or more 32-bit pattern words
        '''
        self._push_words(self.CMD.SEQ_PUSH_PATTERN, patterns)

    def load_glitch(self, glitch: Glitch):
        '''
        Compile DMA stream from glitch builder and upload to device
        '''
        settings = self.get_settings()
        if settings["freq"] != round(glitch.freq):
            raise ValueError(f"device runs at {settings['freq']} Hz, sequence was built for {glitch.freq:.0f} Hz")

        if settings["state"] == self.State.ARMED:
            raise GlitcherError("cannot load while armed")

        zeros_first, durations, patterns = compile_dma_sequence(glitch.seq)
        if len(durations) > self.MAX_BLOCKS or len(patterns) > self.MAX_PATTERN_WORDS:
            raise ValueError(f"sequence too long: {len(durations)}/{self.MAX_BLOCKS}, {len(patterns)}/{self.MAX_PATTERN_WORDS}")

        if settings["state"] == self.State.TRIGGERED:
            self.disarm()

        self.seq_init(zeros_first)
        self.seq_push_duration(*durations)
        self.seq_push_pattern(*patterns)

    def seq_push_switcher(self, mask: int, duration: int):
        '''
        Push switcher command to sequencer
        '''
        self._command(self.CMD.SEQ_PUSH_SWITCHER, "II", mask, duration)

    def set_freq(self, freq: int):
        '''
        Set glitch module f_cpu in Hz
        '''
        if freq > 250e6:
            print("Warning: f_cpu > 250MHz may not work reliably")
        self._command(self.CMD.SET_FREQ, "I", int(freq))

    def get_trigger_state(self) -> "Glitcher.State":
        '''
        Get trigger state
        '''
        return self.State(self._command(self.CMD.GET_STATUS, reply=1)[0])

    def reboot(self, bootloader: bool = False):
        '''
        Reboot glitcher
        '''
        self.ser.write(struct.pack("<BB", self.CMD.REBOOT.value[0], bool(bootloader)))
        self.ser.close()

    def gpio_write(self, pin: int, value: bool):
        '''
        Write value to GPIO pin. Pin will be configured as output
        '''
        self._command(self.CMD.GPIO_WRITE, "BB", pin, bool(value))

    def gpio_read(self, pin: int) -> bool:
        '''
        Read value from GPIO pin. Pin will be configured as input
        '''
        return bool(self._command(self.CMD.GPIO_READ, "B", pin, reply=1)[0])

    def gpio_set_pull(self, pin: int, up: bool, down: bool):
        '''
        Set pull-up/down for GPIO pin
        '''
        self._command(self.CMD.GPIO_SET_PULL, "BB", pin, (bool(up) << 0) | (bool(down) << 1))

    def get_settings(self):
        '''
        Get current glitch module settings
        '''
        state, rising_edge, trigger_edges, freq = struct.unpack(
            "<BBII", self._command(self.CMD.GET_SETTINGS, reply=10))
        return {
            "state": self.State(state),
            "rising_edge": bool(rising_edge),
            "trigger_edges": trigger_edges,
            "freq": freq
        }

    def close(self):
        '''
        Close the device
        '''
        self.ser.close()
