# Controls and User Behavior

## Transmitter inputs

| Input | Pin | Role in supplied WROOM firmware |
|---|---:|---|
| B1 | GPIO13 | Left |
| B2 | GPIO14 | Right |
| B3 | GPIO25 | Mode toggle |
| B4 | GPIO26 | Back |
| B5 | GPIO4 | Forward |
| Joystick X | GPIO36 | Horizontal axis |
| Joystick Y | GPIO39 | Vertical axis |
| Joystick SW | GPIO35 | Button bit 5 |
| Touch | I2C | Coordinates + gesture code |
| MPU6050 | I2C | Gyro + acceleration |

## Modes

The supplied transmitter has two packet modes:

- `mode = 0`: MOUSE
- `mode = 1`: KEYBOARD-assist

B3 toggles the mode on a press edge.

## Joystick

The firmware samples the joystick during startup to calculate the center point. A dead zone is applied around the center to reduce unwanted cursor movement.

The transmitted joystick range is approximately:

```text
-1000 ... 0 ... +1000
```

## Touch

The CST816D is read directly over I2C. The transmitter sends the touch coordinates and a gesture code in `ControllerPacket`.

The exact Windows action associated with each gesture belongs to the receiver firmware; keep that mapping documented in the receiver code and update this file if the mapping changes.

## Startup behavior

At boot:

1. Joystick calibration starts.
2. Gyro calibration starts if the MPU6050 is detected.
3. Keep the joystick centered.
4. Keep the controller still.
5. After calibration, normal packet transmission begins.
