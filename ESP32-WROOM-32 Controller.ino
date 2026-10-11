#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ---------------- PIN CONFIGURATION ----------------
#define BUTTON_B1 13
#define BUTTON_B2 14
#define BUTTON_B3 25
#define BUTTON_B4 26

#define JOYSTICK_X 34
#define JOYSTICK_Y 35

// Replace this with the MAC address printed by the ESP32-S3 receiver.
uint8_t receiverMAC[] = {
  0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
};

// ---------------- MODES ----------------
enum ControlMode : uint8_t {
  MOUSE_MODE = 0,
  KEYBOARD_MODE = 1
};

ControlMode currentMode = MOUSE_MODE;

// ---------------- PACKET FORMAT ----------------
// Keep this structure identical on both boards.
typedef struct __attribute__((packed)) {
  uint8_t mode;
  int8_t dx;
  int8_t dy;
  uint8_t events;
  uint16_t sequence;
} FrameMindPacket;

FrameMindPacket packet;
uint16_t sequenceNumber = 0;

// Event bits: sent only when a button is pressed.
#define EVENT_B1 0x01
#define EVENT_B2 0x02
#define EVENT_B4 0x04

const int JOYSTICK_CENTER = 2048;
const int JOYSTICK_DEADZONE = 350;
const int MAX_CURSOR_STEP = 8;

bool previousB1 = HIGH;
bool previousB2 = HIGH;
bool previousB3 = HIGH;
bool previousB4 = HIGH;

uint32_t lastDebounceB1 = 0;
uint32_t lastDebounceB2 = 0;
uint32_t lastDebounceB3 = 0;
uint32_t lastDebounceB4 = 0;

const uint32_t DEBOUNCE_MS = 35;

bool stablePressed(uint8_t pin, bool &previousReading,
                   uint32_t &lastChange, bool &pressedEvent) {
  bool reading = digitalRead(pin);
  uint32_t now = millis();

  if (reading != previousReading) {
    previousReading = reading;
    lastChange = now;
  }

  // Detect a stable press (HIGH -> LOW with INPUT_PULLUP).
  static bool stableState[40];
  static bool initialized[40];

  if (!initialized[pin]) {
    stableState[pin] = reading;
    initialized[pin] = true;
  }

  if (now - lastChange >= DEBOUNCE_MS &&
      reading != stableState[pin]) {
    stableState[pin] = reading;

    if (reading == LOW) {
      pressedEvent = true;
    }
  }

  return stableState[pin] == LOW;
}

int8_t joystickAxis(int rawValue, bool invertAxis = false) {
  int offset = rawValue - JOYSTICK_CENTER;

  if (abs(offset) < JOYSTICK_DEADZONE) {
    return 0;
  }

  int adjusted = offset;

  if (adjusted > 0) {
    adjusted -= JOYSTICK_DEADZONE;
  } else {
    adjusted += JOYSTICK_DEADZONE;
  }

  int value = map(
    constrain(adjusted, -2048, 2047),
    -2048, 2047,
    -MAX_CURSOR_STEP, MAX_CURSOR_STEP
  );

  if (invertAxis) {
    value = -value;
  }

  return (int8_t)constrain(value, -MAX_CURSOR_STEP, MAX_CURSOR_STEP);
}

void sendPacket() {
  packet.sequence = sequenceNumber++;

  esp_err_t result = esp_now_send(
    receiverMAC,
    (uint8_t *)&packet,
    sizeof(packet)
  );

  if (result != ESP_OK) {
    Serial.printf("ESP-NOW send error: %d\n", result);
  }
}

void onDataSent(const wifi_tx_info_t *info,
                esp_now_send_status_t status) {
  // Keep callback lightweight; avoid printing every packet.
}

void setup() {
  Serial.begin(115200);

  pinMode(BUTTON_B1, INPUT_PULLUP);
  pinMode(BUTTON_B2, INPUT_PULLUP);
  pinMode(BUTTON_B3, INPUT_PULLUP);
  pinMode(BUTTON_B4, INPUT_PULLUP);

  // ADC1 pins; joystick X -> GPIO34, Y -> GPIO35.
  analogReadResolution(12);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.print("FrameMind Controller MAC: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW initialization failed.");
    while (true) delay(1000);
  }

  esp_now_register_send_cb(onDataSent);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, receiverMAC, 6);
  peerInfo.channel = 0; // Use the current Wi-Fi channel.
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add receiver peer.");
    while (true) delay(1000);
  }

  packet.mode = currentMode;
  packet.dx = 0;
  packet.dy = 0;
  packet.events = 0;

  Serial.println("FrameMind controller ready.");
  Serial.println("B1=Click/Enter, B2=Right-click/Escape");
  Serial.println("B3=Toggle mode, B4=Back/Escape");
}

void loop() {
  bool b1Event = false;
  bool b2Event = false;
  bool b3Event = false;
  bool b4Event = false;

  stablePressed(BUTTON_B1, previousB1, lastDebounceB1, b1Event);
  stablePressed(BUTTON_B2, previousB2, lastDebounceB2, b2Event);
  stablePressed(BUTTON_B3, previousB3, lastDebounceB3, b3Event);
  stablePressed(BUTTON_B4, previousB4, lastDebounceB4, b4Event);

  // B3 toggles the operating mode.
  if (b3Event) {
    currentMode = (currentMode == MOUSE_MODE)
                    ? KEYBOARD_MODE
                    : MOUSE_MODE;

    Serial.print("Mode: ");
    Serial.println(currentMode == MOUSE_MODE ? "MOUSE" : "KEYBOARD");
  }

  packet.mode = currentMode;
  packet.events = 0;

  if (b1Event) packet.events |= EVENT_B1;
  if (b2Event) packet.events |= EVENT_B2;
  if (b4Event) packet.events |= EVENT_B4;

  // Joystick movement is active in Mouse mode only.
  if (currentMode == MOUSE_MODE) {
    int rawX = analogRead(JOYSTICK_X);
    int rawY = analogRead(JOYSTICK_Y);

    packet.dx = joystickAxis(rawX, false);
    packet.dy = joystickAxis(rawY, true);
  } else {
    packet.dx = 0;
    packet.dy = 0;
  }

  sendPacket();

  // 100 packets/second target.
  delay(10);
}
