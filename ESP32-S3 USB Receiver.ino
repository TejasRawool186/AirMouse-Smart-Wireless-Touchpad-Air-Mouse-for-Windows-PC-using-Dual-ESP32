#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "USB.h"
#include "USBHIDMouse.h"
#include "USBHIDKeyboard.h"

// ============================================================
// ESP32-S3 RECEIVER
// ============================================================

#define ESPNOW_CHANNEL 1

USBHIDMouse Mouse;
USBHIDKeyboard Keyboard;

// ------------------------------------------------------------
// MUST MATCH TRANSMITTER PACKET
// ------------------------------------------------------------

struct ControllerPacket {
  uint16_t magic;
  uint8_t  version;
  uint8_t  mode;
  uint8_t  buttons;
  uint8_t  gesture;
  uint32_t seq;

  int16_t  joy_x;
  int16_t  joy_y;

  uint16_t touch_x;
  uint16_t touch_y;

  int16_t  gyro_x;
  int16_t  gyro_y;
  int16_t  gyro_z;

  int16_t  accel_x;
  int16_t  accel_y;
  int16_t  accel_z;

  uint32_t uptime_ms;
};

static_assert(sizeof(ControllerPacket) <= 250,
              "Packet too large for ESP-NOW");

// ------------------------------------------------------------
// RECEIVED DATA
// ------------------------------------------------------------

volatile bool newPacket = false;
volatile uint32_t packetCount = 0;
volatile uint32_t lastPacketMillis = 0;

ControllerPacket rxPacket;

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// ------------------------------------------------------------
// STATE
// ------------------------------------------------------------

uint8_t previousButtons = 0;
uint8_t currentMode = 0;
uint32_t lastSequence = 0;

float remainderX = 0;
float remainderY = 0;

// ============================================================
// ESP-NOW RECEIVE CALLBACK
// ============================================================

void onDataReceive(
  const esp_now_recv_info_t *info,
  const uint8_t *data,
  int len
) {
  (void)info;

  if (len != sizeof(ControllerPacket)) {
    return;
  }

  ControllerPacket temp;

  memcpy(&temp, data, sizeof(temp));

  // Check packet signature
  if (temp.magic != 0xC0DE) {
    return;
  }

  if (temp.version != 1) {
    return;
  }

  portENTER_CRITICAL_ISR(&mux);

  rxPacket = temp;
  newPacket = true;
  packetCount++;
  lastPacketMillis = millis();

  portEXIT_CRITICAL_ISR(&mux);
}

// ============================================================
// START ESP-NOW
// ============================================================

bool startESPNow() {

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  delay(100);

  Serial.print("Receiver MAC: ");
  Serial.println(WiFi.macAddress());

  if (esp_wifi_set_channel(
        ESPNOW_CHANNEL,
        WIFI_SECOND_CHAN_NONE) != ESP_OK) {

    Serial.println("ERROR: WiFi channel setup failed");
    return false;
  }

  if (esp_now_init() != ESP_OK) {

    Serial.println("ERROR: ESP-NOW init failed");
    return false;
  }

  esp_now_register_recv_cb(onDataReceive);

  Serial.println("ESP-NOW receiver ready");

  return true;
}

// ============================================================
// RELEASE HID
// ============================================================

void releaseAllHID() {

  Mouse.release(MOUSE_LEFT);
  Mouse.release(MOUSE_RIGHT);
  Mouse.release(MOUSE_MIDDLE);
  Mouse.release(MOUSE_BACKWARD);
  Mouse.release(MOUSE_FORWARD);

  Keyboard.releaseAll();

  previousButtons = 0;
}

// ============================================================
// MOUSE BUTTONS
// ============================================================

void processMouseButtons(uint8_t buttons) {

  uint8_t changed =
    buttons ^ previousButtons;

  // B1 = LEFT
  if (changed & (1 << 0)) {

    if (buttons & (1 << 0))
      Mouse.press(MOUSE_LEFT);
    else
      Mouse.release(MOUSE_LEFT);
  }

  // B2 = RIGHT
  if (changed & (1 << 1)) {

    if (buttons & (1 << 1))
      Mouse.press(MOUSE_RIGHT);
    else
      Mouse.release(MOUSE_RIGHT);
  }

  // B4 = BACK
  if (changed & (1 << 3)) {

    if (buttons & (1 << 3))
      Mouse.press(MOUSE_BACKWARD);
    else
      Mouse.release(MOUSE_BACKWARD);
  }

  // B5 = FORWARD
  if (changed & (1 << 4)) {

    if (buttons & (1 << 4))
      Mouse.press(MOUSE_FORWARD);
    else
      Mouse.release(MOUSE_FORWARD);
  }

  // Joystick button = MIDDLE
  if (changed & (1 << 5)) {

    if (buttons & (1 << 5))
      Mouse.press(MOUSE_MIDDLE);
    else
      Mouse.release(MOUSE_MIDDLE);
  }

  previousButtons = buttons;
}

// ============================================================
// KEYBOARD BUTTONS
// ============================================================

void processKeyboardButtons(uint8_t buttons) {

  uint8_t changed =
    buttons ^ previousButtons;

  // B1 = ENTER
  if (changed & (1 << 0)) {

    if (buttons & (1 << 0))
      Keyboard.press(KEY_RETURN);
    else
      Keyboard.release(KEY_RETURN);
  }

  // B2 = ESC
  if (changed & (1 << 1)) {

    if (buttons & (1 << 1))
      Keyboard.press(KEY_ESC);
    else
      Keyboard.release(KEY_ESC);
  }

  // B4 = LEFT
  if (changed & (1 << 3)) {

    if (buttons & (1 << 3))
      Keyboard.press(KEY_LEFT_ARROW);
    else
      Keyboard.release(KEY_LEFT_ARROW);
  }

  // B5 = RIGHT
  if (changed & (1 << 4)) {

    if (buttons & (1 << 4))
      Keyboard.press(KEY_RIGHT_ARROW);
    else
      Keyboard.release(KEY_RIGHT_ARROW);
  }

  // Joystick button = SPACE
  if (changed & (1 << 5)) {

    if (buttons & (1 << 5))
      Keyboard.press(KEY_SPACE);
    else
      Keyboard.release(KEY_SPACE);
  }

  previousButtons = buttons;
}

// ============================================================
// JOYSTICK → MOUSE
// ============================================================

void processMouseMovement(
  const ControllerPacket &p
) {

  float x =
    (float)p.joy_x / 1000.0f;

  float y =
    (float)p.joy_y / 1000.0f;

  float moveX = x * 7.0f;
  float moveY = y * 7.0f;

  // Small gyro contribution
  float gx =
    (float)p.gyro_x / 1000.0f;

  float gy =
    (float)p.gyro_y / 1000.0f;

  if (fabs(gy) > 0.08f)
    moveX += gy * 1.5f;

  if (fabs(gx) > 0.08f)
    moveY -= gx * 1.5f;

  remainderX += moveX;
  remainderY += moveY;

  int mx = (int)remainderX;
  int my = (int)remainderY;

  remainderX -= mx;
  remainderY -= my;

  mx = constrain(mx, -127, 127);
  my = constrain(my, -127, 127);

  if (mx != 0 || my != 0) {

    Mouse.move(
      (int8_t)mx,
      (int8_t)my
    );
  }
}

// ============================================================
// TOUCH GESTURES
// ============================================================

void processGesture(uint8_t gesture) {

  switch (gesture) {

    // Single tap
    case 0x05:
      Mouse.click(MOUSE_LEFT);
      Serial.println("GESTURE: TAP");
      break;

    // Double tap
    case 0x0B:
      Mouse.click(MOUSE_LEFT);
      delay(40);
      Mouse.click(MOUSE_LEFT);
      Serial.println("GESTURE: DOUBLE TAP");
      break;

    // Long press
    case 0x0C:
      Mouse.click(MOUSE_RIGHT);
      Serial.println("GESTURE: LONG PRESS");
      break;

    // Swipe left
    case 0x03:
      Mouse.click(MOUSE_BACKWARD);
      Serial.println("GESTURE: LEFT");
      break;

    // Swipe right
    case 0x04:
      Mouse.click(MOUSE_FORWARD);
      Serial.println("GESTURE: RIGHT");
      break;

    // Swipe up
    case 0x01:
      Keyboard.write(KEY_PAGE_UP);
      Serial.println("GESTURE: UP");
      break;

    // Swipe down
    case 0x02:
      Keyboard.write(KEY_PAGE_DOWN);
      Serial.println("GESTURE: DOWN");
      break;

    default:
      break;
  }
}

// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println();
  Serial.println("================================");
  Serial.println(" ESP32-S3 USB HID RECEIVER");
  Serial.println("================================");

  // Start USB HID
  Mouse.begin();
  Keyboard.begin();
  USB.begin();

  Serial.println("USB HID started");

  // Start ESP-NOW
  if (!startESPNow()) {

    Serial.println(
      "ESP-NOW START FAILED"
    );

  } else {

    Serial.println(
      "ESP-NOW STARTED"
    );
  }

  Serial.println();
  Serial.println(
    "Waiting for controller packets..."
  );
}

// ============================================================
// LOOP
// ============================================================

void loop() {

  ControllerPacket packet;
  bool received = false;

  // Safely copy latest packet
  portENTER_CRITICAL(&mux);

  if (newPacket) {

    packet = rxPacket;
    newPacket = false;
    received = true;
  }

  uint32_t count = packetCount;
  uint32_t age =
    millis() - lastPacketMillis;

  portEXIT_CRITICAL(&mux);

  // ----------------------------------------------------------
  // PROCESS PACKET
  // ----------------------------------------------------------

  if (received) {

    if (packet.seq != lastSequence) {

      lastSequence = packet.seq;

      // Mode changed
      if (packet.mode != currentMode) {

        releaseAllHID();

        currentMode =
          packet.mode;

        Serial.print("MODE: ");

        Serial.println(
          currentMode == 0
            ? "MOUSE"
            : "KEYBOARD"
        );
      }

      // Mouse mode
      if (currentMode == 0) {

        processMouseButtons(
          packet.buttons
        );

        processMouseMovement(
          packet
        );

      }

      // Keyboard mode
      else {

        processKeyboardButtons(
          packet.buttons
        );
      }

      // Touch
      if (packet.gesture != 0) {

        processGesture(
          packet.gesture
        );
      }
    }
  }

  // ----------------------------------------------------------
  // SAFETY TIMEOUT
  // ----------------------------------------------------------

  if (age > 500) {

    releaseAllHID();

    remainderX = 0;
    remainderY = 0;
  }

  // ----------------------------------------------------------
  // STATUS
  // ----------------------------------------------------------

  static uint32_t lastStatus = 0;

  if (millis() - lastStatus >= 1000) {

    lastStatus = millis();

    Serial.print("Packets: ");
    Serial.print(count);

    Serial.print(" | Age: ");
    Serial.print(age);

    Serial.print(" ms | Sequence: ");
    Serial.print(lastSequence);

    Serial.print(" | Mode: ");

    Serial.println(
      currentMode == 0
        ? "MOUSE"
        : "KEYBOARD"
    );
  }

  delay(1);
}
