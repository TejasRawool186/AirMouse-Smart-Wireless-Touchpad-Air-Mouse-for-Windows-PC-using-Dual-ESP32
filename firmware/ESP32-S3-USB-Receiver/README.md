# ESP32-S3 USB Receiver

This directory is reserved for the **ESP32-S3 native USB HID receiver firmware**.

The uploaded file supplied to the project under the name `ESP32-S3 USB Receiver.ino` was inspected and found to contain the WROOM-32 transmitter sketch instead of S3 receiver code. It is preserved as:

`ESP32-S3-USB-Receiver.uploaded.ino`

Do not flash that file to the S3 as receiver firmware.

Once the correct receiver sketch is supplied, place it here as:

`ESP32-S3-USB-Receiver.ino`

and update:

- `docs/BUILD_AND_FLASH.md`
- `docs/ARCHITECTURE.md`
- `docs/CONTROLS.md`
- `docs/TEST_PLAN.md`
