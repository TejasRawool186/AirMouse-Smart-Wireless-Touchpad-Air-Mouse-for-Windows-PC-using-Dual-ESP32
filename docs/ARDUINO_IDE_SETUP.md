# Arduino IDE 2 Setup Manual

This document is the team setup procedure for compiling and uploading the project.

## 1. Install Arduino IDE 2

Install the current Arduino IDE 2 release for your operating system from Arduino's official software page.

Official documentation:
- https://docs.arduino.cc/software/ide-v2/
- https://docs.arduino.cc/software/ide-v2/tutorials/ide-v2-board-manager/

## 2. Install the ESP32 board package

1. Open Arduino IDE 2.
2. Open **Tools → Board → Boards Manager** or use the Boards Manager icon.
3. Search for `esp32`.
4. Install **esp32 by Espressif Systems**.
5. Use the same Arduino-ESP32 major/minor version across the team. The supplied transmitter source targets Arduino-ESP32 3.x.
6. Restart Arduino IDE if the board list does not refresh.

Espressif's current installation guide provides the stable Boards Manager package URL and the recommended Arduino IDE installation method:
https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html

## 3. Install transmitter libraries

Open **Tools → Manage Libraries...** and install:

| Library | Used by |
|---|---|
| Arduino_GFX_Library | Waveshare LCD |
| Adafruit GFX Library | Graphics base / OLED support |
| Adafruit SSD1306 | SSD1306 OLED |
| Adafruit MPU6050 | MPU6050 |
| Adafruit Unified Sensor | Adafruit MPU6050 dependency |
| Adafruit BusIO | Adafruit library dependency; normally installed automatically |

The transmitter sketch directly reads the CST816D over I2C, so it does **not** require a separate CST816D library.

## 4. Select the WROOM-32 transmitter board

Use the board definition that matches the physical ESP32-WROOM-32 development board being used by the team. If the exact development board is unknown, `ESP32 Dev Module` is commonly appropriate for generic WROOM-32 development boards, but verify the board's USB/UART hardware before selecting it.

Recommended starting settings for a typical generic WROOM-32 board:

| Setting | Value |
|---|---|
| Board | ESP32 Dev Module |
| Upload Speed | 921600 if stable; otherwise 115200 |
| CPU Frequency | Board default |
| Flash Frequency | Board default |
| Flash Mode | Board default |
| Partition Scheme | Default |
| Port | The COM port belonging to the WROOM board |

Do not blindly copy these values to a custom board. Board selection controls how the compiler maps hardware and features.

## 5. Select the ESP32-S3 receiver board

For the receiver, select the exact ESP32-S3 board variant used by the team. If it is a generic S3 development board, `ESP32S3 Dev Module` may be appropriate.

The S3 has native USB peripheral support, so USB device/HID configuration depends on the board variant and Arduino-ESP32 version (e.g., USB CDC on Boot: Enabled, USB Mode: Hardware CDC and JTAG / USB-OTG). Espressif documents USB device support for ESP32-S3 and the relevant USB menu options here:
https://docs.espressif.com/projects/arduino-esp32/en/latest/api/usb.html

For S3 development, keep a known-good USB/CDC configuration documented by the exact board used. If the board exposes native USB, the first flash may require BOOT + RESET/download mode depending on the board and upload configuration.

## 6. Serial Monitor

The supplied transmitter calls:

```cpp
Serial.begin(115200);
```

Set the Arduino IDE Serial Monitor to **115200 baud**.

Expected startup messages include:

```text
CST816D @0x15: OK
MPU6050 @0x68: OK
MPU6050 ready
SSD1306 ready @0x3C
ESP-NOW transmitter ready
Ready.
```

Exact output varies depending on which optional device is detected.

## 7. Verify before upload

Click **Verify** before **Upload**.

If verification succeeds, click **Upload**.

If upload fails:

1. Confirm the correct COM port.
2. Try a known-good data USB cable.
3. Disconnect external wiring temporarily if it interferes with boot.
4. Hold the board's BOOT button during upload if the board requires manual download mode.
5. Lower Upload Speed to 115200.
6. Press RESET after a successful upload if the board does not automatically reboot.

Espressif's troubleshooting guide specifically recommends checking the cable, USB port, power and BOOT/download mode for common upload problems:
https://docs.espressif.com/projects/arduino-esp32/en/latest/troubleshooting.html

## 8. Team version control rule

Record the exact board-package version in the project release notes whenever a known-good firmware build is created. Do not allow every developer to silently change ESP32 core versions while debugging a hardware problem.
