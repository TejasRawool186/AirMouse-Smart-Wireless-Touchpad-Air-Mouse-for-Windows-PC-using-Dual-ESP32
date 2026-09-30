# ESP32-WROOM-32 Controller

This is the transmitter firmware supplied for the project.

It reads:

- Waveshare 2-inch touch LCD / CST816D
- MPU6050
- SSD1306 OLED
- analog joystick
- joystick push switch
- five tactile buttons

and transmits a `ControllerPacket` over ESP-NOW.
