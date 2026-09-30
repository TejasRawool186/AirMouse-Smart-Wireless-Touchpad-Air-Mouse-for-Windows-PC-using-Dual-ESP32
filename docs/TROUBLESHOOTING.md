# Troubleshooting

## 1. Arduino IDE does not show the board

- Use a known-good USB data cable.
- Try another USB port.
- Avoid USB hubs during initial flashing.
- Install the Espressif ESP32 board package.
- Check Device Manager on Windows for a newly created COM port.

## 2. Upload fails with connection timeout

Try:

1. Select the correct board.
2. Select the correct port.
3. Press and hold BOOT.
4. Start upload.
5. Release BOOT when the IDE reports that flashing has started.
6. If necessary, reduce upload speed to 115200.

Espressif documents these common upload checks:
https://docs.espressif.com/projects/arduino-esp32/en/latest/troubleshooting.html

## 3. `MPU6050 NOT FOUND`

Check:

- VCC → 3V3
- GND → GND
- SDA → GPIO21
- SCL → GPIO22
- AD0 → GND
- module actually uses address `0x68`
- SDA/SCL wires are not swapped

## 4. `CST816D @0x15: NOT FOUND`

Check:

- TP_SDA → GPIO21
- TP_SCL → GPIO22
- TP_RST → GPIO32
- TP_INT → GPIO34
- display module is powered
- I2C wiring has a common GND

## 5. OLED does not appear

The supplied code tries addresses `0x3C` and `0x3D`. Check SDA/SCL and power. The OLED is optional in the supplied transmitter code, so the controller can continue without it.

## 6. LCD is blank

Check:

- 3V3 → display 3V3
- GND → display GND
- MOSI → GPIO23
- MISO → GPIO19
- SCLK → GPIO18
- CS → GPIO5
- DC → GPIO27
- RESET → GPIO33
- BL → GPIO17

Also verify that the physical display is the same ST7789-based Waveshare 2-inch module expected by the project.

## 7. Button behaves backwards

That is expected with `INPUT_PULLUP` if interpreted incorrectly. The electrical behavior is:

```text
Released = HIGH
Pressed  = LOW
```

If a 4-pin switch behaves permanently pressed, identify the two internal pin pairs with a multimeter and use one leg from each pair.

## 8. Joystick cursor drifts

Power-cycle the controller with the joystick centered so the startup calibration can measure its center. Check that the joystick output pins are connected to GPIO36 and GPIO39 and that the joystick is powered correctly.

## 9. ESP-NOW does not communicate

Check that both ESP32 devices:

- use the same Wi-Fi/ESP-NOW channel;
- initialize ESP-NOW successfully;
- are powered reliably;
- use compatible packet structure/version.

The supplied transmitter explicitly uses channel 1.

## 10. Windows does not see the receiver as a mouse/keyboard

The ESP32-S3 must run a native USB HID **receiver** sketch. The uploaded file currently named `ESP32-S3 USB Receiver.ino` is not that firmware; it contains the transmitter code and is preserved unchanged for traceability.

Do not troubleshoot Windows HID enumeration until the correct receiver firmware has been flashed.
