# Physical Connection Checklist

Print this page while assembling the controller.

## Power

- [ ] ESP32 3V3 → LCD 3V3
- [ ] ESP32 3V3 → MPU6050 VCC
- [ ] ESP32 3V3 → OLED VCC
- [ ] ESP32 3V3 → joystick VCC
- [ ] LCD VCC → NC
- [ ] LCD GND → ESP32 GND
- [ ] MPU6050 GND → ESP32 GND
- [ ] OLED GND → ESP32 GND
- [ ] Joystick GND → ESP32 GND

## LCD

- [ ] MISO → GPIO19
- [ ] MOSI → GPIO23
- [ ] SCLK → GPIO18
- [ ] SD_CS → NC
- [ ] LCD_CS → GPIO5
- [ ] LCD_DC → GPIO27
- [ ] LCD_RST → GPIO33
- [ ] LCD_BL → GPIO17
- [ ] TP_SDA → GPIO21
- [ ] TP_SCL → GPIO22
- [ ] TP_INT → GPIO34
- [ ] TP_RST → GPIO32

## MPU6050

- [ ] SDA → GPIO21
- [ ] SCL → GPIO22
- [ ] AD0 → GND
- [ ] XDA → NC
- [ ] XCL → NC
- [ ] INT → NC

## OLED

- [ ] SDA → GPIO21
- [ ] SCL → GPIO22

## Joystick

- [ ] VRX → GPIO36
- [ ] VRY → GPIO39
- [ ] SW → GPIO35

## Buttons

- [ ] B1 → GPIO13 + GND
- [ ] B2 → GPIO14 + GND
- [ ] B3 → GPIO25 + GND
- [ ] B4 → GPIO26 + GND
- [ ] B5 → GPIO4 + GND
- [ ] Each button pair verified with continuity meter

## Final inspection

- [ ] No 5 V signal is connected to a 3.3 V GPIO.
- [ ] No two output devices are accidentally tied together.
- [ ] All modules share GND.
- [ ] No accidental short between 3V3 and GND.
