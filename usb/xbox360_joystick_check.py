#!/usr/bin/env python3
"""
Read live joystick/button values from an Xbox 360 controller on Linux.
Target USB ID: 045e:028e (Microsoft Xbox360 Controller)

Requires:
    pip install evdev

Run:
    python3 xbox360_joystick_check.py
"""

from evdev import InputDevice, ecodes, list_devices

TARGET_VENDOR = 0x045E
TARGET_PRODUCT = 0x028E


AXIS_NAMES = {
    ecodes.ABS_X: "LX",
    ecodes.ABS_Y: "LY",
    ecodes.ABS_RX: "RX",
    ecodes.ABS_RY: "RY",
    ecodes.ABS_Z: "LT",
    ecodes.ABS_RZ: "RT",
    ecodes.ABS_HAT0X: "DPAD_X",
    ecodes.ABS_HAT0Y: "DPAD_Y",
}


def normalize(value: int, minimum: int, maximum: int) -> float:
    if maximum == minimum:
        return 0.0
    return ((value - minimum) / (maximum - minimum)) * 2.0 - 1.0


def find_xbox360_device():
    for path in list_devices():
        dev = InputDevice(path)
        if dev.info.vendor == TARGET_VENDOR and dev.info.product == TARGET_PRODUCT:
            caps = dev.capabilities()
            if ecodes.EV_ABS in caps or ecodes.EV_KEY in caps:
                return dev
    return None


def main():
    dev = find_xbox360_device()
    if dev is None:
        print("Xbox 360 controller not found (045e:028e).")
        print("Tip: run `lsusb` and make sure it is connected and recognized.")
        return

    print(f"Using device: {dev.path} | {dev.name}")
    print("Press Ctrl+C to stop.\n")

    abs_info_cache = {}

    for axis in AXIS_NAMES:
        try:
            abs_info_cache[axis] = dev.absinfo(axis)
        except OSError:
            pass

    print("Axis ranges:")
    for axis_code, info in abs_info_cache.items():
        axis_name = AXIS_NAMES.get(axis_code, ecodes.ABS.get(axis_code, str(axis_code)))
        print(f"  {axis_name:7} min={info.min:6} max={info.max:6} flat={info.flat:4}")
    print()

    try:
        for event in dev.read_loop():
            if event.type == ecodes.EV_ABS:
                axis_name = AXIS_NAMES.get(event.code, ecodes.ABS.get(event.code, str(event.code)))
                info = abs_info_cache.get(event.code)
                if info is not None and event.code not in (ecodes.ABS_HAT0X, ecodes.ABS_HAT0Y):
                    n = normalize(event.value, info.min, info.max)
                    print(f"ABS  {axis_name:7} raw={event.value:6} norm={n:+.3f}")
                else:
                    print(f"ABS  {axis_name:7} value={event.value}")

            elif event.type == ecodes.EV_KEY:
                key_name = ecodes.BTN.get(event.code, str(event.code))
                if isinstance(key_name, tuple):
                    key_name = key_name[0]
                state = "DOWN" if event.value else "UP"
                print(f"KEY  {key_name:12} {state}")

    except KeyboardInterrupt:
        print("\nStopped.")


if __name__ == "__main__":
    main()
