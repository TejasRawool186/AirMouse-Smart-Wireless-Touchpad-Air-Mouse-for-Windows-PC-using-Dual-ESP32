# Build and Flash Procedure

## A. Transmitter

### Step 1 — Assemble

Complete the connections in `WIRING.md` before applying power.

### Step 2 — Connect USB

Connect the WROOM-32 development board to the computer using a **data-capable** USB cable.

### Step 3 — Select board and port

In Arduino IDE:

- Tools → Board → select the correct ESP32 board.
- Tools → Port → select the COM port for the transmitter.

### Step 4 — Verify

Open:

`firmware/ESP32-WROOM-32-Controller/ESP32-WROOM-32-Controller.ino`

Click **Verify**.

### Step 5 — Upload

Click **Upload**.

If the board waits for download mode, hold BOOT while the IDE starts uploading, then release it when flashing begins.

### Step 6 — Open Serial Monitor

Set the baud rate to `115200`.

### Step 7 — Startup calibration

During startup:

1. Keep the joystick centered.
2. Keep the controller physically still while the gyro is calibrated.
3. Do not move the joystick during calibration.

## B. Receiver

The receiver must be an actual ESP32-S3 USB-HID sketch. The file currently supplied under `ESP32-S3 USB Receiver.ino` is identical in content to the WROOM transmitter sketch, so it must not be treated as the receiver firmware.

When the correct S3 receiver sketch is available:

1. Select the exact ESP32-S3 board.
2. Configure the native USB device/CDC/HID options appropriate to that board.
3. Select the receiver COM/USB port.
4. Verify.
5. Upload.
6. Connect the S3 native USB port to Windows.
7. Check Windows Device Manager for the USB HID device.

Espressif documents native USB device support for ESP32-S3 and the Arduino-ESP32 USB configuration here:
https://docs.espressif.com/projects/arduino-esp32/en/latest/api/usb.html

## C. First complete-system test

Do not begin with every input at once. Use this order:

1. Transmitter boots.
2. LCD starts.
3. CST816D is detected.
4. MPU6050 is detected.
5. OLED is detected if fitted.
6. Buttons report expected states.
7. Joystick values move from negative to positive around the calibrated center.
8. ESP-NOW packets increment.
9. Receiver gets packets.
10. USB HID enumerates on Windows.
11. Test one mouse action.
12. Test the remaining controls.
