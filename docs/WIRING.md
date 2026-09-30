# Complete Wiring Manual

This is the one-by-one connection reference for the transmitter hardware described by the supplied WROOM-32 sketch.

> **Power rule:** All modules share GND. Use the 3.3 V rail for the 3.3 V modules described by the supplied sketch. Never connect a signal pin to a different voltage just because the connector physically fits.

## 1. ESP32-WROOM-32 transmitter master map

| ESP32 GPIO | Function | Device |
|---:|---|---|
| GPIO4 | B5 button | Push button 5 |
| GPIO5 | LCD CS | Waveshare LCD |
| GPIO13 | B1 button | Push button 1 |
| GPIO14 | B2 button | Push button 2 |
| GPIO17 | LCD backlight | Waveshare LCD |
| GPIO18 | SPI SCLK | Waveshare LCD |
| GPIO19 | SPI MISO | Waveshare LCD |
| GPIO21 | I2C SDA | LCD touch + MPU6050 + OLED |
| GPIO22 | I2C SCL | LCD touch + MPU6050 + OLED |
| GPIO23 | SPI MOSI | Waveshare LCD |
| GPIO25 | B3 button | Push button 3 |
| GPIO26 | B4 button | Push button 4 |
| GPIO27 | LCD DC | Waveshare LCD |
| GPIO32 | Touch reset | CST816D |
| GPIO33 | LCD reset | Waveshare LCD |
| GPIO34 | Touch interrupt input | CST816D |
| GPIO35 | Joystick switch | Joystick |
| GPIO36 | Joystick X ADC | Joystick |
| GPIO39 | Joystick Y ADC | Joystick |

## 2. Waveshare 2-inch touch LCD

The supplied sketch expects these exact connections:

| Waveshare pin | Connect to |
|---|---|
| VCC | **NC** in this wiring configuration |
| 3V3 | ESP32 3V3 |
| GND | ESP32 GND |
| MISO | ESP32 GPIO19 |
| MOSI | ESP32 GPIO23 |
| SCLK | ESP32 GPIO18 |
| SD_CS | **NC** |
| LCD_CS | ESP32 GPIO5 |
| LCD_DC | ESP32 GPIO27 |
| LCD_RST | ESP32 GPIO33 |
| LCD_BL | ESP32 GPIO17 |
| TP_SDA | ESP32 GPIO21 |
| TP_SCL | ESP32 GPIO22 |
| TP_INT | ESP32 GPIO34 |
| TP_RST | ESP32 GPIO32 |

`SD_CS` is unused because the supplied firmware does not use the TF/SD interface.

### Signal path

```text
ESP32 GPIO23 ───────── MOSI
ESP32 GPIO18 ───────── SCLK
ESP32 GPIO19 ───────── MISO
ESP32 GPIO5  ───────── LCD_CS
ESP32 GPIO27 ───────── LCD_DC
ESP32 GPIO33 ───────── LCD_RST
ESP32 GPIO17 ───────── LCD_BL
ESP32 GPIO21 ───────── TP_SDA
ESP32 GPIO22 ───────── TP_SCL
ESP32 GPIO34 ───────── TP_INT
ESP32 GPIO32 ───────── TP_RST
```

## 3. MPU6050

| MPU6050 pin | Connect to |
|---|---|
| VCC | ESP32 3V3 |
| GND | ESP32 GND |
| SCL | ESP32 GPIO22 |
| SDA | ESP32 GPIO21 |
| AD0 | ESP32 GND |
| XDA | NC |
| XCL | NC |
| INT | NC |

With AD0 tied to GND, the supplied firmware expects address `0x68`.

## 4. SSD1306 128×64 OLED

| OLED pin | Connect to |
|---|---|
| VCC | ESP32 3V3 |
| GND | ESP32 GND |
| SDA | ESP32 GPIO21 |
| SCL | ESP32 GPIO22 |

The supplied firmware checks OLED address `0x3C` first and then `0x3D`.

## 5. Joystick

| Joystick pin | Connect to |
|---|---|
| VCC | ESP32 3V3 |
| GND | ESP32 GND |
| VRX | ESP32 GPIO36 |
| VRY | ESP32 GPIO39 |
| SW | ESP32 GPIO35 |

The joystick X/Y inputs are configured as 12-bit ADC inputs.

## 6. Four-pin tactile push buttons

A 4-pin tactile switch normally contains two internally common pins on one side and two internally common pins on the other side. It behaves electrically as one switch between those two sides.

### How to identify the pairs

Do not guess the orientation.

1. Disconnect power.
2. Put the multimeter in continuity mode.
3. Test the four legs in pairs without pressing the button.
4. Two legs will show continuity because they are the same electrical node.
5. The other two legs form the second electrical node.
6. Choose one leg from each node for GPIO and GND.

Example:

```text
       leg A       leg B
         o           o
         |           |
       [ same ]   [ same ]
         |           |
         o           o
       leg C       leg D
```

The exact physical orientation changes when the button is rotated, so use the continuity test rather than assuming left/right.

### Button wiring

| Button | ESP32 GPIO | Other terminal |
|---|---:|---|
| B1 | GPIO13 | GND |
| B2 | GPIO14 | GND |
| B3 | GPIO25 | GND |
| B4 | GPIO26 | GND |
| B5 | GPIO4 | GND |

The firmware uses `INPUT_PULLUP`, so an unpressed button reads HIGH and a pressed button reads LOW.

## 7. Shared I2C bus

Three devices use the same I2C bus:

```text
                 ┌── CST816D touch SDA
GPIO21 SDA ──────┼── MPU6050 SDA
                 └── SSD1306 SDA

                 ┌── CST816D touch SCL
GPIO22 SCL ──────┼── MPU6050 SCL
                 └── SSD1306 SCL
```

This is intentional. I2C is a shared bus and each device has its own address.

## 8. Receiver physical connection

The intended receiver is an ESP32-S3 connected to the Windows PC through its native USB device port. It does not need the transmitter's LCD, MPU6050, joystick or buttons.

```text
ESP32-S3 receiver USB ───────── USB data cable ───────── Windows PC
```

## 9. Circuit diagrams

Refer to the visual schematics in the repository root:
- **Main circuit schematic**: [`circuit_image -main.png`](../circuit_image%20-main.png)
- **Circuit wiring overview**: [`circuit_image.png`](../circuit_image.png)
