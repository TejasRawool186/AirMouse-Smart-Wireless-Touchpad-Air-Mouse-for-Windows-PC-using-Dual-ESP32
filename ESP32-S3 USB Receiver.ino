/*
  USB HID Receiver - ESP32-S2

  Receives ControllerPacket by ESP-NOW and exposes a native USB
  composite HID mouse + keyboard to Windows/Linux/macOS.

  Arduino-ESP32 target: 3.x

  Arduino IDE for ESP32-S2:
    - Board: ESP32S2 Dev Module (or your exact S2 board)
    - USB mode: USB-OTG (TinyUSB) / native USB device mode
    - Use the native USB port for the PC connection.

  No receiver-side external wiring is required other than USB.
*/

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "USB.h"
#include "USBHIDMouse.h"
#include "USBHIDKeyboard.h"

#if !defined(CONFIG_IDF_TARGET_ESP32S2)
#error "Select an ESP32-S2 board for this sketch."
#endif

#if defined(ARDUINO_USB_MODE) && (ARDUINO_USB_MODE == 1)
#error "Set Tools -> USB Mode -> USB-OTG (TinyUSB) for native HID device mode."
#endif

static constexpr uint8_t ESPNOW_CHANNEL = 1;

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

static_assert(sizeof(ControllerPacket) <= 250, "ESP-NOW packet is too large");

USBHIDMouse Mouse;
USBHIDKeyboard Keyboard;

static volatile bool packetReady = false;
static volatile uint32_t lastPacketMillis = 0;
static volatile uint32_t receivedPackets = 0;
static portMUX_TYPE packetMux = portMUX_INITIALIZER_UNLOCKED;
static ControllerPacket rxPacket{};

static uint8_t lastButtons = 0;
static uint8_t lastMode = 0;
static uint32_t lastSeq = 0;
static uint8_t lastGestureProcessed = 0;
static bool usbReady = false;

static float mouseFracX = 0.0f;
static float mouseFracY = 0.0f;
static int16_t keyRepeatX = 0;
static int16_t keyRepeatY = 0;

// -------------------------- Receive callback -------------------------
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  (void)info;
  if (len != static_cast<int>(sizeof(ControllerPacket))) return;

  ControllerPacket p;
  memcpy(&p, data, sizeof(p));

  if (p.magic != 0xC0DE || p.version != 1) return;

  portENTER_CRITICAL_ISR(&packetMux);
  rxPacket = p;
  packetReady = true;
  lastPacketMillis = millis();
  ++receivedPackets;
  portEXIT_CRITICAL_ISR(&packetMux);
}

// -------------------------- ESP-NOW ---------------------------------
bool initEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(50);

  if (esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
    Serial.println("Failed to set Wi-Fi channel");
    return false;
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return false;
  }

  esp_now_register_recv_cb(onDataRecv);

  Serial.print("Receiver MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.println("ESP-NOW receiver ready");
  return true;
}

// -------------------------- HID helpers ------------------------------
static void setMouseButton(uint8_t bit, uint8_t mouseMask, bool pressed) {
  if (!usbReady) return;
  if (pressed) Mouse.press(mouseMask);
  else Mouse.release(mouseMask);
  (void)bit;
}

static void setKeyboardButton(uint8_t key, bool pressed) {
  if (!usbReady) return;
  if (pressed) Keyboard.press(key);
  else Keyboard.release(key);
}

static void releaseAllControlKeys() {
  Keyboard.releaseAll();
  Mouse.release(MOUSE_LEFT);
  Mouse.release(MOUSE_RIGHT);
  Mouse.release(MOUSE_MIDDLE);
  Mouse.release(MOUSE_BACKWARD);
  Mouse.release(MOUSE_FORWARD);
}

static void handleModeChange(uint8_t mode) {
  if (mode != lastMode) {
    releaseAllControlKeys();
    lastMode = mode;
    Serial.print("Mode -> ");
    Serial.println(mode == 0 ? "MOUSE" : "KEYBOARD");
  }
}

static void handleButtonEdges(uint8_t buttons, uint8_t mode) {
  // B3 is handled as mode on the transmitter, so do not use it as a HID key.
  const uint8_t changed = buttons ^ lastButtons;
  if (changed == 0) return;

  if (mode == 0) {
    // Mouse mode
    if (changed & (1u << 0)) setMouseButton(0, MOUSE_LEFT,     (buttons & (1u << 0)) != 0);
    if (changed & (1u << 1)) setMouseButton(1, MOUSE_RIGHT,    (buttons & (1u << 1)) != 0);
    if (changed & (1u << 3)) setMouseButton(3, MOUSE_BACKWARD, (buttons & (1u << 3)) != 0);
    if (changed & (1u << 4)) setMouseButton(4, MOUSE_FORWARD,  (buttons & (1u << 4)) != 0);
    if (changed & (1u << 5)) setMouseButton(5, MOUSE_MIDDLE,   (buttons & (1u << 5)) != 0);
  } else {
    // Keyboard-assist mode
    if (changed & (1u << 0)) setKeyboardButton(KEY_RETURN,      (buttons & (1u << 0)) != 0);
    if (changed & (1u << 1)) setKeyboardButton(KEY_ESC,         (buttons & (1u << 1)) != 0);
    if (changed & (1u << 3)) setKeyboardButton(KEY_LEFT_ARROW,  (buttons & (1u << 3)) != 0);
    if (changed & (1u << 4)) setKeyboardButton(KEY_RIGHT_ARROW, (buttons & (1u << 4)) != 0);
    if (changed & (1u << 5)) setKeyboardButton(KEY_SPACE,       (buttons & (1u << 5)) != 0);
  }

  lastButtons = buttons;
}

static void processGesture(uint8_t gesture) {
  if (!usbReady || gesture == 0) return;
  if (gesture == lastGestureProcessed) return;
  lastGestureProcessed = gesture;

  // CST816 family gesture codes:
  // 0x01 up, 0x02 down, 0x03 left, 0x04 right,
  // 0x05 single tap, 0x0B double tap, 0x0C long press.
  switch (gesture) {
    case 0x05: // single tap
      Mouse.click(MOUSE_LEFT);
      break;

    case 0x0B: // double tap
      Mouse.click(MOUSE_LEFT);
      delay(35);
      Mouse.click(MOUSE_LEFT);
      break;

    case 0x0C: // long press
      Mouse.click(MOUSE_RIGHT);
      break;

    case 0x03: // slide left
      Mouse.click(MOUSE_BACKWARD);
      break;

    case 0x04: // slide right
      Mouse.click(MOUSE_FORWARD);
      break;

    case 0x01: // slide up
      Keyboard.write(KEY_PAGE_UP);
      break;

    case 0x02: // slide down
      Keyboard.write(KEY_PAGE_DOWN);
      break;

    default:
      break;
  }
}

static void processMouseMotion(const ControllerPacket &p) {
  // Joystick produces the main cursor motion.
  float jx = static_cast<float>(p.joy_x) / 1000.0f;
  float jy = static_cast<float>(p.joy_y) / 1000.0f;

  // Small amount of IMU contribution. The gyro is sent in milli-rad/s.
  float gx = static_cast<float>(p.gyro_x) / 1000.0f;
  float gy = static_cast<float>(p.gyro_y) / 1000.0f;

  // The joystick is the primary cursor control; gyro is fine/quick motion.
  float dx = jx * 7.0f + gy * 1.6f;
  float dy = jy * 7.0f - gx * 1.6f;

  // Small deadzone on IMU contribution.
  if (fabsf(gx) < 0.08f) dx -= gy * 1.6f;
  if (fabsf(gy) < 0.08f) dy += gx * 1.6f;

  mouseFracX += dx;
  mouseFracY += dy;

  int moveX = static_cast<int>(truncf(mouseFracX));
  int moveY = static_cast<int>(truncf(mouseFracY));

  mouseFracX -= moveX;
  mouseFracY -= moveY;

  if (moveX != 0 || moveY != 0) {
    moveX = constrain(moveX, -127, 127);
    moveY = constrain(moveY, -127, 127);
    Mouse.move(static_cast<int8_t>(moveX), static_cast<int8_t>(moveY));
  }
}

// -------------------------- Setup -----------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("========================================");
  Serial.println("ESP32-S2 USB HID RECEIVER - START");
  Serial.println("========================================");

  if (!initEspNow()) {
    Serial.println("ESP-NOW startup failed. Receiver will keep retrying nothing.");
  }

  // Native USB HID device.
  Mouse.begin();
  Keyboard.begin();
  USB.begin();
  usbReady = true;

  delay(1200); // allow USB host enumeration
  Serial.println("USB HID mouse + keyboard started");
}

// -------------------------- Loop ------------------------------------
void loop() {
  ControllerPacket p{};
  bool havePacket = false;

  portENTER_CRITICAL(&packetMux);
  if (packetReady) {
    p = rxPacket;
    packetReady = false;
    havePacket = true;
  }
  uint32_t packetAge = millis() - lastPacketMillis;
  uint32_t packetCount = receivedPackets;
  portEXIT_CRITICAL(&packetMux);

  if (havePacket) {
    // Ignore duplicated/out-of-order packets.
    if (p.seq != lastSeq) {
      lastSeq = p.seq;
      handleModeChange(p.mode);
      handleButtonEdges(p.buttons, p.mode);
      processGesture(p.gesture);

      if (p.mode == 0) {
        processMouseMotion(p);
      }
    }
  }

  // Safety: if the wireless link disappears, release all held inputs.
  if (packetAge > 500) {
    if (lastButtons != 0 || lastMode != 0) {
      releaseAllControlKeys();
      lastButtons = 0;
      lastMode = 0;
      mouseFracX = 0;
      mouseFracY = 0;
    }
  }

  static uint32_t lastPrint = 0;
  if (millis() - lastPrint >= 1000) {
    lastPrint = millis();
    Serial.printf("Packets=%lu age=%lums seq=%lu mode=%u\n",
                  static_cast<unsigned long>(packetCount),
                  static_cast<unsigned long>(packetAge),
                  static_cast<unsigned long>(lastSeq),
                  p.mode);
  }

  delay(1);
}
