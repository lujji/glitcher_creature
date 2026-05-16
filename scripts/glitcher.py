import serial, serial.tools.list_ports
import struct
from enum import Enum, auto

class Glitcher:
    USB_VID = 0x16c0
    USB_PID = 0x05e1

    class CMD(Enum):
        ARM = auto(0x80),
        DISARM = auto(),
        FORCE_TRIGGER = auto(),
        SET_HOLDOFF = auto(),
        SET_DURATION = auto(),
        SET_PATTERN = auto(),
        SET_TRIGGER_EDGE = auto(),
        SET_TRIGGER_POLARITY = auto(),
        SET_FREQ = auto(),
        GET_STATUS = auto(),
        GET_SETTINGS = auto(),
        GPIO_WRITE = auto(),
        GPIO_READ = auto(),
        GPIO_SET_PULL = auto(),
        REBOOT = auto(),

    def __init__(self, port:str = None, ignore_ack:bool = False):
        '''
        Connect to device

        param port: Serial port to connect to (e.g. /dev/ttyACM0). If None, will try to find the device automatically
        param ignore_ack: YOLO mode - ignore ACK from device
        '''
        if port is None:
            for i in serial.tools.list_ports.comports():
                    if i.vid == self.USB_VID and i.pid == self.USB_PID and i.interface == "cli":
                        port = i.device
                        break
            else:
                raise Exception("Device not found")
        self.ser = serial.Serial(port, timeout=0.5)
        self.ignore_ack = ignore_ack

    def _get_ack(self) -> bool:
        ack = self.ser.read(2)
        if not ack or len(ack) < 2 and not self.ignore_ack:
            raise Exception("No ACK received")
        if ack[1] != 0xAA:
            return False
        return True

    def arm(self):
        '''
        Arm the glitch module. Glitcher will fire when trigger condition is met
        '''
        self.ser.write(struct.pack("<B", self.CMD.ARM.value[0]))
        return self._get_ack()

    def disarm(self):
        '''
        Disarm the glitcher
        '''
        self.ser.write(struct.pack("<B", self.CMD.DISARM.value[0]))
        return self._get_ack()

    def force_trigger(self):
        '''
        Force the glitch module to trigger immediately
        '''
        self.ser.write(struct.pack("<B", self.CMD.FORCE_TRIGGER.value[0]))
        return self._get_ack()

    def set_holdoff(self, holdoff: int):
        '''
        Set the holdoff time between trigger and glitch in 1/f_cpu units
        '''
        self.ser.write(struct.pack("<BI", self.CMD.SET_HOLDOFF.value[0], holdoff))
        return self._get_ack()

    def set_duration(self, duration: int):
        '''
        Set the duration of the glitch in 1/f_cpu/4 units
        '''
        self.ser.write(struct.pack("<BI", self.CMD.SET_DURATION.value[0], duration))
        return self._get_ack()

    def set_pattern(self, pattern: int):
        '''
        Set the glitch 32-bit pattern.
        Each bit represents one 1/f_cpu/2 pulse where 1 = high and 0 = low
        '''
        self.ser.write(struct.pack("<BI", self.CMD.SET_PATTERN.value[0], pattern))
        return self._get_ack()

    def set_trigger_edge(self, edges: int):
        '''
        Trigger off Nth rising edge. 0 = 1st edge
        '''
        self.ser.write(struct.pack("<BI", self.CMD.SET_TRIGGER_EDGE.value[0], edges))
        return self._get_ack()

    def set_trigger_polarity(self, rising: bool):
        '''
        Set trigger polarity. True = trigger on rising edge, False = trigger on falling edge
        '''
        self.ser.write(struct.pack("<BB", self.CMD.SET_TRIGGER_POLARITY.value[0], 1 if rising else 0))
        return self._get_ack()

    def set_freq(self, freq: int):
        '''
        Set glitch module f_cpu in MHz
        '''
        if freq > 250e6:
            print("Warning: f_cpu > 250MHz may not work reliably")
        self.ser.write(struct.pack("<BI", self.CMD.SET_FREQ.value[0], int(freq)))
        return self._get_ack()

    def get_trigger_state(self) -> bool:
        '''
        Get current trigger state. 0 = not triggered, 1 = triggered
        '''
        self.ser.write(struct.pack("<B", self.CMD.GET_STATUS.value[0]))
        if not self._get_ack():
            raise Exception("Failed to get status ACK")
        data = self.ser.read(1)
        if not data:
            raise Exception("Failed to get trigger state")

        return bool(data[0])

    def reboot(self, bootloader: bool = False):
        '''
        Reboot the device
        '''
        self.ser.write(struct.pack("<BB", self.CMD.REBOOT.value[0], 1 if bootloader else 0))
        return self._get_ack()

    def gpio_write(self, pin: int, value: bool):
        '''
        Write value to GPIO pin. Pin will be configured as output
        '''
        self.ser.write(struct.pack("<BBB", self.CMD.GPIO_WRITE.value[0], pin, 1 if value else 0))
        return self._get_ack()

    def gpio_read(self, pin: int) -> bool:
        '''
        Read value from GPIO pin. Pin will be configured as input
        '''
        self.ser.write(struct.pack("<BB", self.CMD.GPIO_READ.value[0], pin))
        if not self._get_ack():
            raise Exception("Failed to get GPIO read ACK")
        data = self.ser.read(1)
        if not data:
            raise Exception("Failed to read GPIO value")
        return bool(data[0])

    def gpio_set_pull(self, pin: int, up: bool, down: bool):
        '''
        Set pull-up/down for GPIO pin
        '''
        self.ser.write(struct.pack("<BBB", self.CMD.GPIO_SET_PULL.value[0], pin, (up << 0) | (down << 1)))
        return self._get_ack()

    def get_settings(self):
        '''
        Get current glitch module settings
        '''
        self.ser.write(struct.pack("<B", self.CMD.GET_SETTINGS.value[0]))
        if not self._get_ack():
            raise Exception("Failed to get settings ACK")

        data = self.ser.read(19)
        if not data or len(data) < 19:
            raise Exception("Failed to read settings data")

        trigger_state = data[0]
        pattern_mode, rising_edge, holdoff, pattern, edge, freq = struct.unpack("<BBIIII", data[1:20])
        return {
            "trigger_state": trigger_state,
            "pattern_mode": pattern_mode,
            "rising_edge": rising_edge,
            "holdoff": holdoff,
            "pattern": pattern,
            "edge": edge,
            "freq": freq
        }

    def close(self):
        '''
        Close the device
        '''
        self.ser.close()
