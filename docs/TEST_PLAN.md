# Verification Test Plan

Use this checklist before calling a build release-ready.

## Hardware tests

- [ ] All grounds are common.
- [ ] Display 3V3 is connected correctly.
- [ ] Display VCC is NC for this configuration.
- [ ] Display SD_CS is NC.
- [ ] MPU6050 AD0 is tied to GND.
- [ ] XDA/XCL are NC.
- [ ] Five 4-pin buttons were pair-identified with a continuity test.
- [ ] Joystick SW is GPIO35.
- [ ] Joystick X/Y are GPIO36/GPIO39.

## Transmitter boot tests

- [ ] Serial output starts at 115200.
- [ ] CST816D reports `0x15` when present.
- [ ] MPU6050 reports `0x68` when present.
- [ ] OLED reports `0x3C` or `0x3D` when present.
- [ ] LCD backlight turns on.
- [ ] Startup joystick calibration completes.
- [ ] Startup gyro calibration completes when IMU is present.
- [ ] ESP-NOW initializes.
- [ ] Packet sequence increments.

## Input tests

- [ ] B1 changes bit 0.
- [ ] B2 changes bit 1.
- [ ] B3 changes bit 2 and toggles mode.
- [ ] B4 changes bit 3.
- [ ] B5 changes bit 4.
- [ ] Joystick SW changes bit 5.
- [ ] Joystick X moves across its expected range.
- [ ] Joystick Y moves across its expected range.
- [ ] Touch coordinates update.
- [ ] Touch gestures generate non-zero gesture codes.
- [ ] Gyro values change when the controller rotates.

## Wireless tests

- [ ] Receiver is on the same ESP-NOW channel.
- [ ] Receiver validates packet magic.
- [ ] Receiver validates packet version.
- [ ] Receiver sees increasing sequence numbers.
- [ ] Receiver handles a temporary wireless outage.
- [ ] Held HID buttons are released after a link timeout.

## Windows tests

- [ ] ESP32-S3 enumerates as a USB HID device.
- [ ] Mouse pointer moves.
- [ ] Left/right actions work.
- [ ] Back/forward actions work if implemented.
- [ ] Keyboard mode works if implemented.
- [ ] Touch actions work if implemented.
- [ ] Disconnecting the transmitter does not leave a mouse button permanently held.
