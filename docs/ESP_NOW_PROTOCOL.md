# ESP-NOW Packet Protocol

The transmitter and receiver communicate using one fixed binary structure called `ControllerPacket`.

## Packet definition

```cpp
struct ControllerPacket {
  uint16_t magic;
  uint8_t version;
  uint8_t mode;
  uint8_t buttons;
  uint8_t gesture;
  uint32_t seq;
  int16_t joy_x;
  int16_t joy_y;
  uint16_t touch_x;
  uint16_t touch_y;
  int16_t gyro_x;
  int16_t gyro_y;
  int16_t gyro_z;
  int16_t accel_x;
  int16_t accel_y;
  int16_t accel_z;
  uint32_t uptime_ms;
};
```

## Field meanings

| Field | Meaning |
|---|---|
| `magic` | Packet signature; supplied firmware uses `0xC0DE` |
| `version` | Protocol version; supplied firmware uses `1` |
| `mode` | `0` mouse, `1` keyboard-assist |
| `buttons` | Bitfield for B1-B5 and joystick switch |
| `gesture` | CST816D gesture code, `0` when no new gesture is sent |
| `seq` | Monotonically increasing packet sequence |
| `joy_x` | Calibrated joystick X, approximately -1000..1000 |
| `joy_y` | Calibrated joystick Y, approximately -1000..1000 |
| `touch_x` | Touch X, 0..239 |
| `touch_y` | Touch Y, 0..319 |
| `gyro_x/y/z` | Gyro values scaled to milli-rad/s |
| `accel_x/y/z` | Acceleration values scaled by 100 from m/s² |
| `uptime_ms` | Transmitter uptime in milliseconds |

## Button bits

| Bit | Source |
|---:|---|
| 0 | B1 / GPIO13 |
| 1 | B2 / GPIO14 |
| 2 | B3 / GPIO25 |
| 3 | B4 / GPIO26 |
| 4 | B5 / GPIO4 |
| 5 | Joystick SW / GPIO35 |

## Timing

The supplied transmitter sends the normal packet approximately every 20 ms, or about 50 packets per second.

## Validation requirements for receiver

A receiver should validate:

1. packet length equals `sizeof(ControllerPacket)`;
2. `magic == 0xC0DE`;
3. `version == 1`;
4. sequence number is not an old duplicate;
5. packet age is below the configured safety timeout.

## Channel

The supplied transmitter configures ESP-NOW channel 1. The receiver must operate on the same channel.
